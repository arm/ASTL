// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_SYSTEM_INFO_HPP_
#define ASTL_SYSTEM_INFO_HPP_

#include <filesystem>
#include <memory>
#include <string>

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
};

auto GetActivePlatformInfo() -> const PlatformInfoData&;
auto GetHostPlatformInfo() -> const PlatformInfoData&;
auto GetLoadedPlatformInfo() -> std::shared_ptr<const PlatformInfoData>;
auto SavePlatformInfoToCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code;
auto LoadPlatformInfoFromCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code;
auto ClearLoadedPlatformInfo() -> void;

}  // namespace astl

#endif  // ASTL_SYSTEM_INFO_HPP_
