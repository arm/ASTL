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

#include <optional>
#include <string>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "common/scmi/scmi_constants.hpp"
#include "config/configuration_manager.hpp"

namespace astl {

/** @brief if ASTL_SCMI_SYSFS_TELEMETRY_ROOT is set, use that, else use default path */
static auto GetScmiSysFsTelemetryRootPath() -> std::filesystem::path {
  auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_SCMI_SYSFS_TELEMETRY_ROOT);
  if (!env_var_value.empty()) {
    return std::filesystem::path{env_var_value};
  }
  return std::filesystem::path{kDefaultScmiSysfsTelemetryRootPath};
}

/** @brief Look for the ASTL config directory in the following locations in priority order:
 *  1. ASTL_CONFIG_DIR env var
 *  2. user-specific config directory based on OS
 *  3. system-level config directory based on OS
 *  4. relative to library location
 */
static auto GetAstlConfigDirPath() -> std::expected<std::filesystem::path, astl_status_code> {
  // 1. check env var
  auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR);
  if (!env_var_value.empty()) {
    return std::filesystem::path{env_var_value};
  }
  // 2. user-specific config directory
  // @todo(ASTL-274) implement user-specific config directory lookup based on OS
  // 3. system-level config directory
  // @todo(ASTL-274) implement system-level config directory lookup based on OS
  // 4. default to relative path from library location
  auto lib_path = ConfigurationManager::GetAstlFilePath();
  if (lib_path) {
    return lib_path.value().parent_path() / "config";
  } else {
    return std::unexpected(lib_path.error());
  }
}

/** @brief if we're restoring ASTL from a saved file, get that path from ASTL_LOAD_FILE_PATH env variable */
auto GetLoadFilePath() -> std::optional<std::filesystem::path> {
  auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_LOAD_FILE_PATH);
  if (!env_var_value.empty()) {
    return std::filesystem::path{env_var_value};
  }
  return std::nullopt;
}

AstlConfiguration::AstlConfiguration(std::filesystem::path const&                scmi_sysfs_path,
                                     std::filesystem::path const&                config_dir_path,
                                     std::optional<std::filesystem::path> const& load_file_path)
    : scmi_sysfs_telemetry_root_path{scmi_sysfs_path},
      config_dir_path{config_dir_path},
      metrics_dir_path{config_dir_path / "metrics"},
      scmi_specification_dir{config_dir_path / "scmi" / "public"},
      load_file_path{load_file_path} {}

/* Create a AstlConfiguration instance, depending on env variables, and file system paths found. */
[[nodiscard]] auto AstlConfiguration::CreateConfiguration() -> std::expected<AstlConfiguration, astl_status_code> {
  auto scmi_sysfs_path = GetScmiSysFsTelemetryRootPath();
  auto config_dir_path = GetAstlConfigDirPath();
  if (!config_dir_path) {
    return std::unexpected<astl_status_code>(config_dir_path.error());
  }
  auto load_file_path = GetLoadFilePath();
  return AstlConfiguration(scmi_sysfs_path, *config_dir_path, load_file_path);
}

}  // namespace astl
