// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "astl_read_file_handle_cache.hpp"

#include <algorithm>
#include <cerrno>

#ifdef __linux__
#  include <sys/resource.h>
#endif

#include "astl_logger.hpp"

namespace astl {

namespace {

[[nodiscard]] bool IsTooManyOpenFilesError(int error_number) {
#ifdef EMFILE
  if (error_number == EMFILE) {
    return true;
  }
#endif
#ifdef ENFILE
  if (error_number == ENFILE) {
    return true;
  }
#endif
  return false;
}

}  // namespace

astl_status_code ReadFileHandleCache::Read(const std::filesystem::path &path, std::string &output) const {
  std::error_code status_ec;
  const bool      can_cache_stream =
      std::filesystem::is_regular_file(path, status_ec) || std::filesystem::is_symlink(path, status_ec);
  const auto cache_capacity = GetReadCacheCapacity();

  TrimToCapacity(cache_capacity);
  if (!can_cache_stream || cache_capacity == 0U) {
    return ReadWithoutCaching(path, output);
  }

  auto stream_access = GetOrOpen(path, cache_capacity);
  if (!stream_access.has_value()) {
    return stream_access.error();
  }

  auto &file = stream_access->iterator->second.stream;
  if (!stream_access->newly_opened) {
    file.clear();
    file.seekg(0, std::ios::beg);
  }
  if (!stream_access->newly_opened && !file.good()) {
    Evict(stream_access->iterator);
    stream_access = GetOrOpen(path, cache_capacity);
    if (!stream_access.has_value()) {
      return stream_access.error();
    }
  }

  auto &active_file = stream_access->iterator->second.stream;
  output.assign(std::istreambuf_iterator<char>(active_file), {});
  if (active_file.bad()) {
    Evict(stream_access->iterator);
    return ASTL_STATUS_FILE_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

void ReadFileHandleCache::Invalidate(const std::filesystem::path &path) const {
  const auto stream_it = _streams.find(path);
  if (stream_it != _streams.end()) {
    Evict(stream_it);
  }
}

astl_status_code ReadFileHandleCache::ReadWithoutCaching(const std::filesystem::path &path, std::string &output) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return ASTL_STATUS_FILE_OPEN_FAILED;
  }

  output.assign(std::istreambuf_iterator<char>(file), {});
  if (file.bad()) {
    return ASTL_STATUS_FILE_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ReadFileHandleCache::GetReadCacheCapacity() const -> std::size_t {
  if (_maxCachedReadHandles.has_value()) {
    return _maxCachedReadHandles.value();
  }
#ifdef __linux__
  _maxCachedReadHandles = ComputeLinuxReadCacheCapacity();
  return _maxCachedReadHandles.value();
#else
  return kDefaultMaxCachedReadHandles;
#endif
}

#ifdef __linux__
auto ReadFileHandleCache::ComputeLinuxReadCacheCapacity() -> std::size_t {
  constexpr std::size_t num_reserved_file_handles = 32;

  const auto descriptor_limit  = GetSoftFileHandleLimit();
  const auto open_handle_count = GetOpenFileHandleCount();
  if (!descriptor_limit.has_value() || !open_handle_count.has_value()) {
    return kDefaultMaxCachedReadHandles;
  }

  if (descriptor_limit.value() <= open_handle_count.value() + num_reserved_file_handles) {
    return 0U;
  }

  return std::min(kDefaultMaxCachedReadHandles,
                  descriptor_limit.value() - open_handle_count.value() - num_reserved_file_handles);
}

auto ReadFileHandleCache::GetSoftFileHandleLimit() -> std::optional<std::size_t> {
  struct rlimit limits{};
  if (getrlimit(RLIMIT_NOFILE, &limits) != 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(limits.rlim_cur);
}

auto ReadFileHandleCache::GetOpenFileHandleCount() -> std::optional<std::size_t> {
  try {
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
      (void)entry;
      ++count;
    }
    return count;
  } catch (const std::exception &e) {
    ASTL_LOG_DEBUG("GetOpenFileHandleCount: {}", e.what());
    return std::nullopt;
  }
}
#endif

void ReadFileHandleCache::TrimToCapacity(std::size_t capacity) const {
  while (_streams.size() > capacity) {
    EvictLeastRecentlyUsed();
  }
}

void ReadFileHandleCache::Touch(StreamMap::iterator stream_it) const {
  // move on up to the front of the line
  _usage.splice(_usage.begin(), _usage, stream_it->second.usage_it);
  stream_it->second.usage_it = _usage.begin();
}

void ReadFileHandleCache::Evict(StreamMap::iterator stream_it) const {
  stream_it->second.stream.close();
  _usage.erase(stream_it->second.usage_it);
  _streams.erase(stream_it);
}

void ReadFileHandleCache::EvictLeastRecentlyUsed() const {
  if (_usage.empty()) {
    return;
  }

  const auto lru_path  = _usage.back();
  const auto stream_it = _streams.find(lru_path);
  if (stream_it == _streams.end()) {
    _usage.pop_back();
    return;
  }
  Evict(stream_it);
}

auto ReadFileHandleCache::Open(const std::filesystem::path &path) const
    -> std::expected<StreamAccess, astl_status_code> {
  _usage.push_front(path);
  auto [stream_it, inserted] = _streams.try_emplace(path, CachedStream{std::ifstream{}, _usage.begin()});
  if (!inserted) {
    _usage.pop_front();
    Touch(stream_it);
    return StreamAccess{stream_it, false};
  }

  stream_it->second.stream.open(path);
  if (!stream_it->second.stream.is_open()) {
    _usage.erase(stream_it->second.usage_it);
    _streams.erase(stream_it);
    return std::unexpected(ASTL_STATUS_FILE_OPEN_FAILED);
  }

  return StreamAccess{stream_it, true};
}

auto ReadFileHandleCache::GetOrOpen(const std::filesystem::path &path, std::size_t capacity) const
    -> std::expected<StreamAccess, astl_status_code> {
  const auto existing_stream = _streams.find(path);
  if (existing_stream != _streams.end()) {
    Touch(existing_stream);
    return StreamAccess{existing_stream, false};
  }

  TrimToCapacity(capacity > 0U ? capacity - 1U : 0U);

  errno              = 0;
  auto opened_stream = Open(path);
  int  open_errno    = errno;
  while (!opened_stream.has_value() && !_streams.empty() && IsTooManyOpenFilesError(open_errno)) {
    EvictLeastRecentlyUsed();
    errno         = 0;
    opened_stream = Open(path);
    open_errno    = errno;
  }
  return opened_stream;
}

}  // namespace astl
