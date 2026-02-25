/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include "common/string_pool.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include "astl_logger.hpp"
#include "nlohmann/json.hpp"

namespace astl {
namespace {

struct StringPoolHash {
  using is_transparent = void;

  auto operator()(std::string_view value) const noexcept -> std::size_t { return std::hash<std::string_view>{}(value); }

  auto operator()(const std::string& value) const noexcept -> std::size_t {
    return std::hash<std::string_view>{}(value);
  }

  auto operator()(const char* value) const noexcept -> std::size_t {
    return std::hash<std::string_view>{}(value == nullptr ? std::string_view{} : std::string_view{value});
  }
};

auto PoolMutex() -> std::shared_mutex& {
  static std::shared_mutex mutex;
  return mutex;
}

auto PoolStorage() -> std::unordered_set<std::string, StringPoolHash, std::equal_to<>>& {
  static std::unordered_set<std::string, StringPoolHash, std::equal_to<>> pool;
  return pool;
}

}  // namespace

auto GetInternedString(std::string_view value) -> const char* {
  {
    std::shared_lock lock{PoolMutex()};
    const auto       iter = PoolStorage().find(value);
    if (iter != PoolStorage().end()) {
      return iter->c_str();
    }
  }

  std::unique_lock lock{PoolMutex()};
  const auto [iter, inserted] = PoolStorage().emplace(value);
  (void)inserted;
  return iter->c_str();
}

auto GetInternedString(const std::string& value) -> const char* { return GetInternedString(std::string_view{value}); }

auto GetInternedString(const char* value) -> const char* {
  if (value == nullptr) {
    return nullptr;
  }
  return GetInternedString(std::string_view{value});
}

namespace testing {

auto RehashStringPoolForTest(std::size_t bucket_count) -> void {
  std::unique_lock lock{PoolMutex()};
  PoolStorage().rehash(bucket_count);
}

auto FindStringPointerForTest(std::string_view value) -> const char* {
  std::shared_lock lock{PoolMutex()};
  const auto       iter = PoolStorage().find(value);
  if (iter == PoolStorage().end()) {
    return nullptr;
  }
  return iter->c_str();
}

auto GetInternedStringPoolBucketCountForTest() -> std::size_t {
  std::shared_lock lock{PoolMutex()};
  return PoolStorage().bucket_count();
}

}  // namespace testing

auto SnapshotStringPool() -> std::vector<std::string> {
  std::shared_lock         lock{PoolMutex()};
  std::vector<std::string> snapshot;
  snapshot.reserve(PoolStorage().size());
  std::copy(PoolStorage().begin(), PoolStorage().end(), std::back_inserter(snapshot));
  return snapshot;
}

auto SaveStringPoolToCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code {
  std::error_code err_code;
  std::filesystem::create_directories(cache_dir_path, err_code);
  if (err_code) {
    ASTL_LOG_ERROR("SaveStringPoolToCacheDir: failed creating cache directory '{}': {}", cache_dir_path.string(),
                   err_code.message());
    return ASTL_STATUS_FILE_ERROR;
  }

  auto snapshot = SnapshotStringPool();
  std::sort(snapshot.begin(), snapshot.end());

  nlohmann::json payload;
  payload["strings"] = snapshot;

  const auto    output_file = cache_dir_path / kStringPoolName;
  std::ofstream stream(output_file, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    ASTL_LOG_ERROR("SaveStringPoolToCacheDir: failed opening '{}'", output_file.string());
    return ASTL_STATUS_FILE_OPEN_FAILED;
  }

  stream << payload.dump();
  if (stream.fail()) {
    ASTL_LOG_ERROR("SaveStringPoolToCacheDir: failed writing '{}'", output_file.string());
    return ASTL_STATUS_FILE_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

auto LoadStringPoolFromCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code {
  const auto input_file = cache_dir_path / kStringPoolName;
  if (!std::filesystem::exists(input_file)) {
    return ASTL_STATUS_SUCCESS;
  }

  std::ifstream stream(input_file, std::ios::binary | std::ios::in);
  if (!stream.is_open()) {
    ASTL_LOG_ERROR("LoadStringPoolFromCacheDir: failed opening '{}'", input_file.string());
    return ASTL_STATUS_FILE_OPEN_FAILED;
  }

  nlohmann::json payload;
  try {
    stream >> payload;
  } catch (const std::exception& e) {
    ASTL_LOG_ERROR("LoadStringPoolFromCacheDir: invalid payload in '{}': {}", input_file.string(), e.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  if (!payload.contains("strings") || !payload["strings"].is_array()) {
    ASTL_LOG_ERROR("LoadStringPoolFromCacheDir: 'strings' array missing in '{}'", input_file.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  for (const auto& value : payload["strings"]) {
    if (!value.is_string()) {
      continue;
    }
    (void)GetInternedString(value.get<std::string>());
  }

  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
