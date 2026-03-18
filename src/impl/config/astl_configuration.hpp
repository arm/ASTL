// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_CONFIGURATION_HPP_
#define ASTL_CONFIGURATION_HPP_

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "common/metric_config.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"

namespace astl {

/** @brief Overall configuration for the ASTL library */
struct AstlConfiguration {
  /** @brief Path to SCMI Sysfs. Defaults to /sys/fs/arm_telemetry,
   * but can be overridden with env var ASTL_SCMI_SYSFS_TELEMETRY_ROOT
   */
  std::filesystem::path scmi_sysfs_telemetry_root_path;

  /** @brief Path to the directory containing ASTL metric definitions and platform-specific SCMI specifications
   * initialized from ASTL_CONFIG_DIR
   */
  std::filesystem::path config_dir_path;

  /** @brief Path to the subdirectory containing ASTL metric definitions, derived from config_dir_path
   */
  std::filesystem::path metrics_dir_path;

  /** @brief Path to the subdirectory containing ASTL metric-group definitions, derived from config_dir_path
   */
  std::filesystem::path groups_dir_path;

  /** @brief Path to the directory containing platform-specific SCMI specifications
   * derived from config_dir_path
   */
  std::filesystem::path scmi_specification_dir;

  /** @brief Path to load ASTL components from a saved session (.astl file). */
  std::optional<std::filesystem::path> load_file_path;

  [[nodiscard]] static auto CreateConfiguration() -> std::expected<AstlConfiguration, astl_status_code>;

 private:
  // private ctor - use CreateConfiguration factory method instead
  AstlConfiguration(std::filesystem::path const& scmi_sysfs_path, std::filesystem::path const& config_dir_path,
                    std::optional<std::filesystem::path> const& load_file_path = std::nullopt);
};

}  // namespace astl

#endif  // ASTL_CONFIGURATION_HPP_
