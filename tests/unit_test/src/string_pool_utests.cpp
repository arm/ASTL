/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../test_utilities.hpp"
#include "common/string_pool.hpp"

namespace {
constexpr std::size_t kRehashBucketCount = 16384;
}

TEST_CASE("StringPool interns identical values", "[string_pool]") {
  const char* first  = astl::GetInternedString(std::string{"metric_power"});
  const char* second = astl::GetInternedString(std::string{"metric_power"});

  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(first == second);
  REQUIRE(std::string(first) == "metric_power");
}

TEST_CASE("StringPool save/load from cache dir", "[string_pool][cache]") {
  namespace fs = std::filesystem;

  TempFileGuard  temp_guard("astl_string_pool_utest");
  const fs::path cache_dir = temp_guard.path;

  const std::string sentinel = "string_from_pool_persistence_test";
  (void)astl::GetInternedString(sentinel);

  REQUIRE(astl::SaveStringPoolToCacheDir(cache_dir) == ASTL_STATUS_SUCCESS);
  REQUIRE(fs::exists(cache_dir / astl::kStringPoolName));

  {
    std::ofstream bad_payload(cache_dir / astl::kStringPoolName, std::ios::out | std::ios::trunc);
    REQUIRE(bad_payload.good());
    bad_payload << R"({"strings":["string_from_pool_persistence_test","string_loaded_from_file"]})";
  }

  REQUIRE(astl::LoadStringPoolFromCacheDir(cache_dir) == ASTL_STATUS_SUCCESS);

  const char* loaded_first  = astl::GetInternedString("string_loaded_from_file");
  const char* loaded_second = astl::GetInternedString("string_loaded_from_file");
  REQUIRE(loaded_first != nullptr);
  REQUIRE(loaded_second != nullptr);
  REQUIRE(loaded_first == loaded_second);
  REQUIRE(std::string(loaded_first) == "string_loaded_from_file");

  const char* loaded_sentinel = astl::GetInternedString(sentinel);
  REQUIRE(loaded_sentinel != nullptr);
  REQUIRE(std::string(loaded_sentinel) == sentinel);
  std::error_code ec;
  fs::remove_all(cache_dir, ec);
}

TEST_CASE("StringPool load is additive and preserves existing strings", "[string_pool][cache][additive]") {
  namespace fs = std::filesystem;

  const fs::path  cache_dir = fs::temp_directory_path() / "astl_string_pool_additive_test";
  std::error_code ec;
  fs::remove_all(cache_dir, ec);

  // Add initial strings to the pool
  const std::string original_string_1 = "original_string_before_save_1";
  const std::string original_string_2 = "original_string_before_save_2";
  const char*       original_ptr_1    = astl::GetInternedString(original_string_1);
  const char*       original_ptr_2    = astl::GetInternedString(original_string_2);

  REQUIRE(original_ptr_1 != nullptr);
  REQUIRE(original_ptr_2 != nullptr);

  // Save pool to cache
  REQUIRE(astl::SaveStringPoolToCacheDir(cache_dir) == ASTL_STATUS_SUCCESS);
  REQUIRE(fs::exists(cache_dir / astl::kStringPoolName));

  // Add new strings AFTER saving (simulating strings added during runtime)
  const std::string runtime_string = "string_added_after_save";
  const char*       runtime_ptr    = astl::GetInternedString(runtime_string);
  REQUIRE(runtime_ptr != nullptr);

  // Load from cache - should be additive, not replace
  REQUIRE(astl::LoadStringPoolFromCacheDir(cache_dir) == ASTL_STATUS_SUCCESS);

  // Verify all strings are still accessible
  const char* reloaded_original_1 = astl::testing::FindStringPointerForTest(original_string_1);
  const char* reloaded_original_2 = astl::testing::FindStringPointerForTest(original_string_2);
  const char* reloaded_runtime    = astl::testing::FindStringPointerForTest(runtime_string);

  REQUIRE(reloaded_original_1 != nullptr);
  REQUIRE(reloaded_original_2 != nullptr);
  REQUIRE(reloaded_runtime != nullptr);

  // Verify pointer stability - existing strings should have same pointers
  REQUIRE(reloaded_original_1 == original_ptr_1);
  REQUIRE(reloaded_original_2 == original_ptr_2);
  REQUIRE(reloaded_runtime == runtime_ptr);

  // Verify content
  REQUIRE(std::string(reloaded_original_1) == original_string_1);
  REQUIRE(std::string(reloaded_original_2) == original_string_2);
  REQUIRE(std::string(reloaded_runtime) == runtime_string);

  fs::remove_all(cache_dir, ec);
}

TEST_CASE("StringPool interned pointer remains stable across explicit rehash", "[string_pool][rehash]") {
  const std::string stable_value = "string_pool_rehash_stability_target";

  for (size_t index = 0; index < 1024; ++index) {
    (void)astl::GetInternedString("string_pool_rehash_fill_" + std::to_string(index));
  }

  const char* pointer_before = astl::GetInternedString(stable_value);
  REQUIRE(pointer_before != nullptr);

  const auto bucket_count_before = astl::testing::GetInternedStringPoolBucketCountForTest();

  astl::testing::RehashStringPoolForTest(kRehashBucketCount);

  const auto  bucket_count_after = astl::testing::GetInternedStringPoolBucketCountForTest();
  const char* pointer_after      = astl::testing::FindStringPointerForTest(stable_value);
  REQUIRE(pointer_after != nullptr);
  REQUIRE(bucket_count_after >= kRehashBucketCount);
  REQUIRE(bucket_count_after != bucket_count_before);
  REQUIRE(pointer_before == pointer_after);
  REQUIRE(std::string(pointer_after) == stable_value);
}