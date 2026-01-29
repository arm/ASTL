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
  /** @brief Default constructor assigns default values to paths.
   * These can be overridden with configuration_data in ParseConfiguration */
  AstlConfiguration();

  /** @brief scmi_sysfs_telemetry_root_override is an optional path to replace "/sys/fs/arm_telemetry"
   *         This is a placeholder example of something that _could_ be configured.
   *         subject to change, not currently modified.
   */
  std::optional<std::filesystem::path> scmi_sysfs_telemetry_root_path;

  /** @brief Override path for the directory containing UUID-mapped ASTL metric config json files.
   *  defaults to ./config/metrics if not set
   */
  std::optional<std::filesystem::path> astl_metrics_dir;

  /** @brief Override path for directory containing UUID-mapped SCMI specification json files.
   *  defaults to ./config/scmi if not set
   */
  std::optional<std::filesystem::path> scmi_specification_dir;

  std::optional<std::filesystem::path> astl_file_path;
};

[[nodiscard]] auto ParseConfiguration(std::string_view configuration_data)
    -> std::expected<AstlConfiguration, astl_status_code>;

[[nodiscard]] auto ParseConfiguration(std::istream& configuration_data)
    -> std::expected<AstlConfiguration, astl_status_code>;

}  // namespace astl

#endif  // ASTL_CONFIGURATION_HPP_
