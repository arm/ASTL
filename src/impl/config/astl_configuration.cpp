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

static auto ValidateAstlConfigDirPath(const std::filesystem::path& config_dir_path) -> astl_status_code {
  if (!std::filesystem::is_directory(config_dir_path)) {
    ASTL_LOG_ERROR("ASTL config directory does not exist: {}", config_dir_path.string());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  // potentially add some more lightweight checks here, maybe a checksum with warnings on modifications?
  return ASTL_STATUS_SUCCESS;
}

/** @brief Helper to locate the OS-specific expected location of the user-level astl config dir
 * Linux:    $XDG_DATA_HOME/astl/ or ~/.local/share/astl
 * Mac:      ~/Library/Application Support/astl/
 * Windows:  %LOCALAPPDATA%\astl\
 *  @return optional path if found and valid, else nullopt
 */
static auto GetUserConfigDirPath() -> std::optional<std::filesystem::path> {
  std::filesystem::path user_config_dir_path;
#ifdef _WIN32
  const auto local_app_data = astl::GetEnvVar(astl::EnvVar::LOCALAPPDATA);
  if (local_app_data.empty()) {
    ASTL_LOG_ERROR("LOCALAPPDATA environment variable not set, cannot determine user config dir path");
    return std::nullopt;
  }
  user_config_dir_path = std::filesystem::path(local_app_data) / "astl";
#elif __APPLE__
  const auto home_dir = astl::GetEnvVar(astl::EnvVar::HOME);
  if (home_dir.empty()) {
    ASTL_LOG_ERROR("HOME environment variable not set, cannot determine user config dir path");
    return std::nullopt;
  }
  user_config_dir_path = std::filesystem::path(home_dir) / "Library" / "Application Support" / "astl";
#else  // Linux and other Unix-like
  // first check if XDG_DATA_HOME is set, if not use ~/.local/share/
  const auto xdg_data_home = astl::GetEnvVar(astl::EnvVar::XDG_DATA_HOME);
  if (!xdg_data_home.empty()) {
    user_config_dir_path = std::filesystem::path(xdg_data_home) / "astl";
  } else {
    const auto home_dir = astl::GetEnvVar(astl::EnvVar::HOME);
    if (home_dir.empty()) {
      ASTL_LOG_ERROR("HOME environment variable not set, cannot determine user config dir path");
      return std::nullopt;
    }
    user_config_dir_path = std::filesystem::path(home_dir) / ".local" / "share" / "astl";
  }
#endif
  const auto validate_result = ValidateAstlConfigDirPath(user_config_dir_path);
  if (validate_result == ASTL_STATUS_SUCCESS) {
    return user_config_dir_path;
  }
  return std::nullopt;
}

/** @brief Helper to locate the OS-specific expected location of the system-level astl config dir
 * Linux:   /usr/local/share/astl/
 * Mac:     /Library/Application Support/astl/
 * Windows: %PROGRAMDATA%\astl\
 * @return optional path if found and valid, else nullopt
 */
static auto GetSystemConfigDirPath() -> std::optional<std::filesystem::path> {
  std::filesystem::path system_config_dir_path;
#ifdef _WIN32
  const auto program_data = astl::GetEnvVar(astl::EnvVar::PROGRAMDATA);
  if (program_data.empty()) {
    ASTL_LOG_ERROR("PROGRAMDATA environment variable not set, cannot determine user config dir path");
    return std::nullopt;
  }
  system_config_dir_path = std::filesystem::path(program_data) / "astl";
#elif __APPLE__
  system_config_dir_path = std::filesystem::path("/Library/Application Support/astl");
#else  // Linux and other Unix-like
  system_config_dir_path = std::filesystem::path("/usr/local/share/astl");
#endif
  const auto validate_result = ValidateAstlConfigDirPath(system_config_dir_path);
  if (validate_result == ASTL_STATUS_SUCCESS) {
    return system_config_dir_path;
  }
  return std::nullopt;
}

/** @brief Look for the ASTL config directory in the following locations in priority order:
 *  1. ASTL_CONFIG_DIR env var
 *  2. user-specific config directory based on OS
 *  3. system-level config directory based on OS
 *  4. relative to library location
 */
static auto GetAstlConfigDirPath() -> std::expected<std::filesystem::path, astl_status_code> {
  // 1. check env var
  const auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_CONFIG_DIR);
  if (!env_var_value.empty()) {
    ASTL_LOG_INFO("Using ASTL config directory from ASTL_CONFIG_DIR env var: {}", env_var_value);
    const std::filesystem::path config_dir_path{env_var_value};
    const auto                  validate_result = ValidateAstlConfigDirPath(config_dir_path);
    if (validate_result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Given ASTL_CONFIG_DIR path is not a valid config directory: {}", env_var_value);
      return std::unexpected<astl_status_code>(validate_result);
    }
    return config_dir_path;
  }

  // 2. user-specific config directory
  const auto user_config_dir_path = GetUserConfigDirPath();
  if (user_config_dir_path.has_value() && !user_config_dir_path->empty()) {
    ASTL_LOG_INFO("Using user-specific ASTL config directory: {}", user_config_dir_path->string());
    return *user_config_dir_path;
  }

  // 3. system-level config directory
  const auto system_config_dir_path = GetSystemConfigDirPath();
  if (system_config_dir_path && !system_config_dir_path->empty()) {
    ASTL_LOG_INFO("Using system-level ASTL config directory: {}", system_config_dir_path->string());
    return *system_config_dir_path;
  }

  // 4. finally, default to relative path from library location
  const auto lib_path = ConfigurationManager::GetAstlFilePath();
  if (lib_path) {
    const auto config_dir_path = lib_path.value().parent_path() / "config";
    const auto validate_result = ValidateAstlConfigDirPath(config_dir_path);
    if (validate_result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Unable to locate config dir relative to astl library: {}", config_dir_path.string());
      return std::unexpected<astl_status_code>(validate_result);
    }
    ASTL_LOG_INFO("Using config dir from path relative to astl library: {}", config_dir_path.string());
    return config_dir_path;
  }
  // X. couldn't find any valid config dir
  ASTL_LOG_ERROR("Unable to locate ASTL config directory - see debug log for paths checked.");
  return std::unexpected(lib_path.error());
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
