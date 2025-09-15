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
 * @brief Returns a list of targets accessible via SCMI on this platform
 *
 * @param configuration The ASTL configuration containing SCMI sysfs path overrides
 * @param scmi_sysfs_file_interface A FileInterface (or mock) to use for exploring the SCMI targets
 */
template <typename FileInterfaceType>
auto ScanForTargetsOnFileInterface(const AstlConfiguration& configuration, FileInterfaceType scmi_sysfs_file_interface)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;
  auto de_implementation_version_path = scmi_sysfs_file_interface.GetBasePath() / "de_implementation_version";

  auto is_valid = scmi_sysfs_file_interface.IsValid(de_implementation_version_path);
  if (!is_valid) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check existence of de_implementation_version");
    return std::unexpected(is_valid.error());
  }
  if (!(is_valid.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not exist.  "
        "Generating 0 targets for SCMI/SysFS");
    return std::vector<std::unique_ptr<ITarget> >();
  }

  auto has_read_permission = scmi_sysfs_file_interface.HasReadPermission(de_implementation_version_path);
  if (!has_read_permission) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check permissions of de_implementation_version");
    return std::unexpected(has_read_permission.error());
  }
  if (!(has_read_permission.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not have read permissions.  "
        "Generating 0 targets for SCMI/SysFS");
    return std::vector<std::unique_ptr<ITarget> >();
  }

  std::string read_content;
  auto        read_status = scmi_sysfs_file_interface.Read(de_implementation_version_path, read_content);
  if (read_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to read content of de_implementation_version");
    return std::unexpected(read_status);
  }

  /// @todo ASTL-165 Get rid of hard-coded target name, single instance
  /// @todo ASTL-166 Create a proper member variable or structure to store the UUID
  (void)configuration;
  ASTL_LOG_INFO("ScmiTopologyPlugin::ScanForTargets: Successfully detected 1 SCMI/SysFS target with UUID {}",
                read_content);
  targets.push_back(std::make_unique<Target>("TLM_0", "UUID:" + read_content, CollectorType::SCMI));

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
