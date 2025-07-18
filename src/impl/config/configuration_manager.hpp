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

auto GetConfiguration(astl_initialization_parameters_t const* init_params)
    -> std::expected<AstlConfiguration, astl_status_code>;

}  // namespace  ConfigurationManager
}  // namespace astl

#endif  // CONFIGURATION_MANAGER_HPP_
