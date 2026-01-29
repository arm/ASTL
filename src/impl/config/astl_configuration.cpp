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

#include "config/astl_configuration.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "config/configuration_manager.hpp"

using json = nlohmann::json;

namespace astl {

AstlConfiguration::AstlConfiguration()
    : scmi_sysfs_telemetry_root_path(std::nullopt),
      astl_metrics_dir(std::nullopt),
      scmi_specification_dir(std::nullopt),
      astl_file_path(std::nullopt) {
  // Set default astl_metrics_dir relative to library location
  if (auto lib_path = ConfigurationManager::GetAstlFilePath()) {
    astl_metrics_dir       = lib_path.value().parent_path() / "config" / "metrics";
    scmi_specification_dir = lib_path.value().parent_path() / "config" / "scmi" / "public";
  }
}

inline auto from_json(const json& json_data, AstlConfiguration& cfg) -> void {
  // optional string: j.value(key, default_opt) works nicely
  //  cfg.scmi_sysfs_telemetry_root_path =
  if (const auto path = json_data.value("scmi_sysfs_telemetry_root_path", ""); !path.empty()) {
    cfg.scmi_sysfs_telemetry_root_path = path;
  }

  if (const auto path = json_data.value("astl_metrics_dir", ""); !path.empty()) {
    cfg.astl_metrics_dir = path;
  }

  // another optional string
  if (const auto path = json_data.value("scmi_specification_dir", ""); !path.empty()) {
    cfg.scmi_specification_dir = path;
  }

  if (const auto path = json_data.value("astl_file_path", ""); !path.empty()) {
    cfg.astl_file_path = path;
  }
}

auto ParseConfiguration(std::string_view configuration_data) -> std::expected<AstlConfiguration, astl_status_code> {
  try {
    json json_data = json::parse(configuration_data);
    return json_data.get<AstlConfiguration>();
  } catch (nlohmann::json::parse_error const& e) {
    ASTL_LOG_ERROR("Parse error reading ASTL config file: {}", e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

auto ParseConfiguration(std::istream& configuration_data) -> std::expected<AstlConfiguration, astl_status_code> {
  if (!configuration_data) {
    ASTL_LOG_ERROR("Null configuration data given to GetConfiguration");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  // Read the entire content of the istream into a string buffer
  std::string file_content((std::istreambuf_iterator<char>(configuration_data)), std::istreambuf_iterator<char>());
  return ParseConfiguration(std::string_view(file_content));
}

}  // namespace astl
