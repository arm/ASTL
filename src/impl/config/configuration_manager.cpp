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

#include <expected>
#include <filesystem>
#include <fstream>

#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"

namespace astl {
namespace ConfigurationManager {

namespace fs = std::filesystem;

auto GetConfiguration(astl_initialization_parameters_t const *init_params)
    -> std::expected<AstlConfiguration, astl_status_code> {
  if (init_params->_configuration_file_path == nullptr) {
    // nullptr is valid - just use default settings
    ASTL_LOG_DEBUG("No configuration file path given, using default config settings");
    return AstlConfiguration{};
  }
  std::filesystem::path config_filepath{init_params->_configuration_file_path};
  std::ifstream         config_file_ifstream(config_filepath);
  if (!config_file_ifstream) {
    ASTL_LOG_ERROR("Unable to open {} as input configuration file", config_filepath.string());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  ASTL_LOG_DEBUG("Parsing ASTL configuration from {}", config_filepath.string());
  return ParseConfiguration(config_file_ifstream);
};

}  // namespace ConfigurationManager

}  // namespace astl
