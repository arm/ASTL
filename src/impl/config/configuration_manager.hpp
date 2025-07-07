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

#ifndef CONFIGURATION_MANAGER_HPP_
#define CONFIGURATION_MANAGER_HPP_

#include <filesystem>
#include <optional>
#include <vector>

namespace astl {

struct AstlConfiguration {
  /** @brief scmi_sysfs_telemetry_root_override is an optional path to replace "/tmp/fuse/scmi/scmi_telemetry"
   *         This is a placeholder example of something that _could_ be configured.
   *         subject to change, not currently modified.
   */
  std::optional<std::filesystem::path> scmi_sysfs_telemetry_root_override;

  /** @brief disabled_metrics_by_name is a collection of metric names to disable
   *         This is a placeholder example of something that _could_ be configured.
   *         subject to change, not currently modified.
   */
  std::vector<std::string> disabled_metrics_by_name;
};

namespace ConfigurationManager {

AstlConfiguration GetConfiguration();

}  // namespace ConfigurationManager

}  // namespace astl

#endif  // CONFIGURATION_MANAGER_HPP_
