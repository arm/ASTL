// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_SYSTEM_INFO_HPP_
#define ASTL_SYSTEM_INFO_HPP_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "astl/astl_errors.h"

namespace astl {

constexpr const char* kPlatformInfoFileName = "platform_info.astl";

struct PlatformInfoData {
  std::string soc_name;
  std::string vendor_id;
  std::string os_name;
  std::string kernel_name;
  std::string kernel_version;
  std::string kernel_release;
  std::string firmware_version;
  std::string hostname;
  std::string architecture;
  std::string cpu_type;
  std::string cpu_features;
  std::string cache_info;
  uint32_t    core_count{0};
  uint32_t    numa_node_count{0};
  uint32_t    socket_count{0};
  uint32_t    cache_line_size_bytes{0};
  uint64_t    memory_total_bytes{0};
  std::string libc_version;
  std::string boot_info;
  int64_t     huge_pages_total{-1};
  int64_t     huge_page_size_kb{-1};
  std::string transparent_huge_pages;
};

auto GetActivePlatformInfo() -> const PlatformInfoData&;
auto GetHostPlatformInfo() -> const PlatformInfoData&;
auto GetLoadedPlatformInfo() -> std::shared_ptr<const PlatformInfoData>;
auto SavePlatformInfoToCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code;
auto LoadPlatformInfoFromCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code;
auto ClearLoadedPlatformInfo() -> void;

namespace detail {
auto InferCpuTypeFromCpuInfoText(std::string_view cpuinfo_contents) -> std::string;
}  // namespace detail

}  // namespace astl

#endif  // ASTL_SYSTEM_INFO_HPP_
