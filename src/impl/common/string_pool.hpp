/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#ifndef STRING_POOL_HPP_
#define STRING_POOL_HPP_

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "astl/astl_errors.h"

namespace astl {

// String pool for deduplication and stable string pointer management.
// Interns strings to provide stable const char* pointers that remain valid
// for the lifetime of the string pool. Multiple calls with identical string
// values return the same pointer, enabling efficient comparison and storage.
constexpr const char* kStringPoolName = "string_pool.json";

// Intern a string in the pool, returning a stable pointer to the deduplicated value
auto GetInternedString(std::string_view value) -> const char*;
auto GetInternedString(const std::string& value) -> const char*;
auto GetInternedString(const char* value) -> const char*;

// Test helper: forces a rehash of the underlying storage to validate pointer stability.
namespace testing {
auto RehashStringPoolForTest(std::size_t bucket_count) -> void;
auto FindStringPointerForTest(std::string_view value) -> const char*;
auto GetInternedStringPoolBucketCountForTest() -> std::size_t;
}  // namespace testing

auto SnapshotStringPool() -> std::vector<std::string>;

auto SaveStringPoolToCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code;
auto LoadStringPoolFromCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code;

}  // namespace astl

#endif  // STRING_POOL_HPP_
