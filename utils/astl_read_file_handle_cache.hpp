// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_READ_FILE_HANDLE_CACHE_HPP
#define ASTL_READ_FILE_HANDLE_CACHE_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

#include "astl/astl_errors.h"

namespace astl {

/** This class provides a cache for read file handles to optimize repeated file reads. */
class ReadFileHandleCache {
 public:
  static constexpr std::size_t kDefaultMaxCachedReadHandles = 64;

  ReadFileHandleCache() = default;
  /**
   * @brief Create a cache with a specified maximum number of cached read handles.
   * If the value is std::nullopt, the cache uses the default cap of 64.
   *
   */
  explicit ReadFileHandleCache(std::optional<std::size_t> maxCachedReadHandles)
      : _maxCachedReadHandles(maxCachedReadHandles) {}

  /** @brief Read the contents of a file, using the cache if possible. */
  astl_status_code Read(const std::filesystem::path &path, std::string &output) const;

  /** @brief Invalidate the cache entry for a specific file path, if it exists. */
  void Invalidate(const std::filesystem::path &path) const;

 private:
  struct CachedStream {
    std::ifstream                              stream;
    std::list<std::filesystem::path>::iterator usage_it;
  };

  using StreamMap = std::unordered_map<std::filesystem::path, CachedStream>;

  struct StreamAccess {
    StreamMap::iterator iterator;
    bool                newly_opened{true};
  };

  static astl_status_code ReadWithoutCaching(const std::filesystem::path &path, std::string &output);

  auto GetReadCacheCapacity() const -> std::size_t;
#ifdef __linux__
  static auto ComputeLinuxReadCacheCapacity() -> std::size_t;
  static auto GetSoftFileHandleLimit() -> std::optional<std::size_t>;
  static auto GetOpenFileHandleCount() -> std::optional<std::size_t>;
#endif

  void TrimToCapacity(std::size_t capacity) const;
  /** @brief Mark a cached stream as recently used. */
  void Touch(StreamMap::iterator stream_it) const;
  void Evict(StreamMap::iterator stream_it) const;
  void EvictLeastRecentlyUsed() const;
  auto Open(const std::filesystem::path &path) const -> std::expected<StreamAccess, astl_status_code>;
  auto GetOrOpen(const std::filesystem::path &path, std::size_t capacity) const
      -> std::expected<StreamAccess, astl_status_code>;

  mutable std::optional<std::size_t>       _maxCachedReadHandles;
  mutable std::list<std::filesystem::path> _usage;
  mutable StreamMap                        _streams;
};

}  // namespace astl

#endif  // ASTL_READ_FILE_HANDLE_CACHE_HPP
