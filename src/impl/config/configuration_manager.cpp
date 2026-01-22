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

#include "config/configuration_manager.hpp"

#if defined(__linux__) || defined(__APPLE__)
#  include <dlfcn.h>
#elif defined(_WIN32)
#  include <windows.h>
#else
#  error "Unsupported Operating System"
#endif

#include <expected>
#include <filesystem>
#include <fstream>

#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"

namespace astl {
namespace ConfigurationManager {

namespace fs = std::filesystem;

auto GetAstlFilePath() -> std::expected<fs::path, astl_status_code> {
#if defined(__linux__) || defined(__APPLE__)
  Dl_info dl_info;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Required to obtain function symbol address
  if (dladdr(reinterpret_cast<void*>(&GetAstlFilePath), &dl_info) == 0) {
    ASTL_LOG_ERROR("Could not find info for shared object when detecting config file path (Linux / Mac)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }

  if (dl_info.dli_fname == nullptr) {
    ASTL_LOG_ERROR("Could not determine path of ASTL .so library (Linux / Mac)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }
  fs::path lib_path{dl_info.dli_fname};
  // If we got an executable (not .so), we're likely statically linked
  // Try to find the library in a relative path (useful for tests)
  // @todo(ASTL-274) look up config files from resource / appdata paths rather or in addition to lib path.
  if (lib_path.extension() != ".so" && !lib_path.filename().string().starts_with("lib")) {
    ASTL_LOG_DEBUG("Detected statically linked binary: {}", lib_path.string());
    // Look for lib directory relative to executable
    auto potential_lib_dir = lib_path.parent_path().parent_path() / "lib";
    if (fs::exists(potential_lib_dir)) {
      ASTL_LOG_INFO("Using library path from build tree: {}", potential_lib_dir.string());
      return potential_lib_dir / "libastl.so";
    }
  }
  return lib_path;
#elif defined(_WIN32)
  HMODULE h_module = nullptr;
  if (!GetModuleHandleExA(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Required to obtain function symbol address
          reinterpret_cast<LPCSTR>(&GetAstlFilePath), &h_module)) {
    ASTL_LOG_ERROR("Could not find info for shared object when detecting config file path (Windows)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }
  char  so_path[MAX_PATH];
  DWORD path_length = GetModuleFileNameA(h_module, so_path, MAX_PATH);
  if (path_length == 0 || path_length == MAX_PATH) {
    ASTL_LOG_ERROR("Could not determine path of ASTL .so library (Windows)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return fs::path(so_path);
#endif
}

auto GetConfigurationEnvironmentVariable() -> std::optional<fs::path> {
  const auto environment_variable_content = astl::GetEnvVar("ASTL_CONFIG_JSON_PATH");
  if (environment_variable_content.empty()) {
    return std::nullopt;
  }
  return fs::path(environment_variable_content);
}

auto GetConfigurationFilePath() -> std::expected<fs::path, astl_status_code> {
  auto config_file_env_var = GetConfigurationEnvironmentVariable();

  fs::path config_file_path;
  if (config_file_env_var) {
    config_file_path = config_file_env_var.value();
    ASTL_LOG_INFO("Using ASTL configuration file path from environment variable ASTL_CONFIG_JSON_PATH: {}",
                  config_file_path.string());
  } else {
    auto astl_so_path = GetAstlFilePath();
    if (!astl_so_path) {
      return std::unexpected<astl_status_code>(astl_so_path.error());
    }

    fs::path astl_so_path_validated = astl_so_path.value();

    // Since this is packaged as part of the library, it's OK for us to hard-code a default name.
    const fs::path config_file_name = "astl_configuration.json";

    config_file_path = astl_so_path_validated.parent_path() / config_file_name;
  }

  if (!fs::exists(config_file_path)) {
    ASTL_LOG_ERROR("ASTL configuration file not found at expected path: {}", config_file_path.string());
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ASTL_LOG_INFO("Using Config File at {}", config_file_path.string());
  return config_file_path;
}

auto GetConfiguration() -> std::expected<AstlConfiguration, astl_status_code> {
  auto config_filepath = GetConfigurationFilePath();
  if (!config_filepath) {
    ASTL_LOG_DEBUG("No configuration override file path specified in ASTL_CONFIG_JSON_PATH, using defaults.");
    return AstlConfiguration{};
  }

  fs::path      config_filepath_validated = config_filepath.value();
  std::ifstream config_file_ifstream(config_filepath_validated);
  if (!config_file_ifstream) {
    ASTL_LOG_ERROR("Unable to open {} as input configuration file", config_filepath_validated.string());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ASTL_LOG_DEBUG("Parsing ASTL configuration from {}", config_filepath_validated.string());
  return ParseConfiguration(config_file_ifstream);
};

}  // namespace ConfigurationManager

}  // namespace astl
