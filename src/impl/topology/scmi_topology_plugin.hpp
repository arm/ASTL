/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#ifndef SCMI_TOPOLOGY_PLUGIN_HPP_
#define SCMI_TOPOLOGY_PLUGIN_HPP_

#include <expected>
#include <memory>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "common/scmi/scmi_constants.hpp"
#include "config/astl_configuration.hpp"
#include "target.hpp"

namespace astl {

namespace ScmiTopologyPlugin {

namespace detail {

/**
 * @brief Returns a Target (e.g. named "tlm-0") accessible via SCMI from the given subdirectory scmi_telemetry
 *
 * @param scmi_sysfs_file_interface a FileInterface implementation (or mock) to use to explore the SCMI targets
 * @param telemetry_subdirectory A string representing the subdirectory under which to look for SCMI targets (e.g.
 * "tlm-0")
 */
template <typename FileInterfaceType>
auto DetectTarget(FileInterfaceType const& scmi_sysfs_file_interface, std::string const& telemetry_subdirectory)
    -> std::expected<std::unique_ptr<ITarget>, astl_status_code> {
  auto de_implementation_version_path =
      scmi_sysfs_file_interface.GetBasePath() / telemetry_subdirectory / "de_implementation_version";

  auto is_valid = scmi_sysfs_file_interface.IsValid(de_implementation_version_path);
  if (!is_valid) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check existence of de_implementation_version");
    return std::unexpected(is_valid.error());
  }
  if (!(is_valid.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not exist.  "
        "skipping target directory {}",
        telemetry_subdirectory);
    return {};
  }

  auto has_read_permission = scmi_sysfs_file_interface.HasReadPermission(de_implementation_version_path);
  if (!has_read_permission) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check permissions of de_implementation_version");
    return std::unexpected(has_read_permission.error());
  }
  if (!(has_read_permission.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not have read permissions.  "
        "skipping target directory {}",
        telemetry_subdirectory);
    return {};
  }

  std::string read_content;
  auto        read_status = scmi_sysfs_file_interface.Read(de_implementation_version_path, read_content);
  if (read_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR(
        "ScmiTopologyPlugin::ScanForTargets: Failed to read content of de_implementation_version "
        "for target {}",
        telemetry_subdirectory);
    return std::unexpected(read_status);
  }
  auto normalized_uuid = scmi::spec::GetNormalizedUuid(read_content);
  ASTL_LOG_INFO("ScmiTopologyPlugin::ScanForTargets: Successfully detected SCMI/SysFS target with UUID {}",
                normalized_uuid);
  auto target_ptr = std::make_unique<Target>(telemetry_subdirectory, "Target discovered via SCMI", CollectorType::SCMI,
                                             nullptr, normalized_uuid);
  return target_ptr;
}

/**
 * @brief Returns a list of targets accessible via SCMI on this platform
 *
 * @param configuration The ASTL configuration containing SCMI sysfs path overrides
 * @param scmi_sysfs_file_interface A FileInterface (or mock) to use for exploring the SCMI targets
 */
template <typename FileInterfaceType>
auto ScanForTargetsOnFileInterface(const AstlConfiguration& configuration, FileInterfaceType scmi_sysfs_file_interface)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;
  (void)configuration;  // currently unused
  if (!scmi_sysfs_file_interface.IsValid(scmi_sysfs_file_interface.GetBasePath())) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: SCMI sysfs telemetry root path is not valid");
    return {};
  }
  auto telemetry_root_children = scmi_sysfs_file_interface.GetSubdirectories();
  if (!telemetry_root_children) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to list children of SCMI sysfs telemetry root");
    return {};
  }

  for (const auto& entry : telemetry_root_children.value()) {
    auto target = DetectTarget(scmi_sysfs_file_interface, entry.path().filename().string());
    if (!target) {
      if (target.error() == ASTL_STATUS_SUCCESS) {
        // Not an error, just no target found in this directory
        continue;
      }
      ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to detect target in directory {}",
                     entry.path().string());
      return std::unexpected(target.error());
    }
    if (target.value()) {
      targets.push_back(std::move(target.value()));
    }
  }
  return targets;
}

}  // namespace detail

/**
 * @brief Returns a list of targets accessible via SCMI on this platform
 */
inline auto ScanForTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  FileInterface scmi_sysfs_file_interface{
      configuration.scmi_sysfs_telemetry_root_path.value_or(kDefaultScmiSysfsTelemetryRootPath)};
  return detail::ScanForTargetsOnFileInterface(configuration, std::move(scmi_sysfs_file_interface));
}

}  // namespace ScmiTopologyPlugin

}  // namespace astl

#endif  // SCMI_TOPOLOGY_PLUGIN_HPP_
