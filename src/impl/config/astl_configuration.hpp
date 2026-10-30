// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "config/config_source.hpp"
#include "scmi/scmi_platform_telemetry_spec.hpp"

namespace astl {

/** @brief Collectors enabled for live topology and metric discovery. */
struct CollectorSelection {
  std::optional<std::vector<CollectorType>> enabled;

  [[nodiscard]] auto IsEnabled(CollectorType collector_type) const -> bool;
};

/** @brief Overall configuration for the ASTL library */
struct AstlConfiguration {
  /** @brief Path to SCMI Sysfs. Defaults to /sys/fs/arm_telemetry,
   * but can be overridden with env var ASTL_SCMI_SYSFS_TELEMETRY_ROOT
   */
  std::filesystem::path scmi_sysfs_telemetry_root_path;

  /** @brief Path to SCMI ioctl character devices. Defaults to /dev/scmi,
   * but can be overridden with env var ASTL_SCMI_IOCTL_DEV_ROOT.
   */
  std::filesystem::path scmi_ioctl_device_root_path;

  /** @brief Path to procfs. Defaults to /proc, but can be overridden with env var ASTL_PROCFS_ROOT. */
  std::filesystem::path procfs_root_path;

  /** @brief NUMA node sysfs root, overridden by ASTL_NUMA_ROOT. */
  std::filesystem::path numa_root_path{"/sys/devices/system/node"};

  /** @brief Logical root of the selected configuration snapshot (empty path)
   */
  std::filesystem::path config_dir_path;

  /** @brief Logical path to the subdirectory containing ASTL metric definitions, derived from config_dir_path
   */
  std::filesystem::path metrics_dir_path;

  /** @brief Logical path to the subdirectory containing ASTL metric-group definitions, derived from config_dir_path
   */
  std::filesystem::path groups_dir_path;

  /** @brief Logical path to platform-specific SCMI specifications
   * derived from config_dir_path
   */
  std::filesystem::path scmi_specification_dir;

  /** @brief Path to load ASTL components from a saved session (.astl file). */
  std::optional<std::filesystem::path> load_file_path;

  /** @brief Collector allowlist used for live discovery. */
  CollectorSelection collectors;

  std::shared_ptr<const config::ConfigSource> resources;

  [[nodiscard]] static auto CreateConfiguration() -> std::expected<AstlConfiguration, astl_status_code>;

 private:
  /**
   * @brief Constructs an ASTL configuration from already resolved paths.
   *
   * Use CreateConfiguration() so environment overrides and path validation are
   * applied consistently.
   *
   * @param scmi_sysfs_path Root of the SCMI sysfs telemetry tree.
   * @param scmi_ioctl_device_root Root containing SCMI ioctl telemetry devices.
   * @param procfs_root_path Root of the procfs telemetry tree.
   * @param config_dir_path Root of ASTL's runtime configuration files.
   * @param load_file_path Optional ASTL session file to load.
   */
  AstlConfiguration(std::filesystem::path const& scmi_sysfs_path, std::filesystem::path const& scmi_ioctl_device_root,
                    std::filesystem::path const& procfs_root_path, std::filesystem::path const& config_dir_path,
                    std::optional<std::filesystem::path> const& load_file_path = std::nullopt);
};

}  // namespace astl

#endif  // ASTL_CONFIGURATION_HPP_
