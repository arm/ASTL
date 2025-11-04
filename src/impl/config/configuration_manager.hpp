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

#include <expected>
#include <filesystem>
#include <optional>
#include <vector>

#include "astl/astl_telemetry.h"
#include "config/astl_configuration.hpp"

namespace astl {
namespace ConfigurationManager {

/**
 * @brief Get the path to the .so / .dll file for the ASTL library
 *
 * @return If successful, returns the path to the ASTL shared object file.
 * If unsuccessful, returns an appropriate astl_status_code error.
 */
auto GetAstlFilePath() -> std::expected<std::filesystem::path, astl_status_code>;

/**
 * @brief Check for the existence of the ASTL_CONFIG_JSON_PATH environment variable and return its value if set
 *
 * @return Checks for the existence of the ASTL_CONFIG_JSON_PATH environment variable. If set and non-empty, returns its
 * value as a filesystem path.
 */
auto GetConfigutationEnvironmentVariable() -> std::optional<std::filesystem::path>;

/**
 * @brief Generate path the the configuration JSON
 *
 * Tries to get the path via @ref GetConfigutationEnvironmentVariable first, and if not set,
 * assumes the configuration file is in the same directory as the ASTL .so / .dll
 *
 * @return The path to the configuration JSON file, choosing the appropriate .so path or environment variable.
 */
auto GetConfigurationFilePath() -> std::expected<std::filesystem::path, astl_status_code>;

/**
 * @brief Determine the path to the configuration JSON file and parse it into an AstlConfiguration object
 *
 * @return If successful, returns the path the configuration JSON file.
 * If unsuccessful, returns an appropriate astl_status_code error.
 */
auto GetConfiguration() -> std::expected<AstlConfiguration, astl_status_code>;

}  // namespace ConfigurationManager

}  // namespace astl

#endif  // CONFIGURATION_MANAGER_HPP_
