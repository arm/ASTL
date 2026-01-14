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

#ifndef TOPOLOGY_BUILDER_HPP_
#define TOPOLOGY_BUILDER_HPP_

#include <expected>
#include <memory>

#include "astl/astl_errors.h"
#include "config/configuration_manager.hpp"
#include "topology/i_topology_manager.hpp"

namespace astl {
/**
 * @brief Initialize a topology manager.  This will run as many topology plugins as possible
 * to discover what is available on the current platform.
 */
[[nodiscard]] auto BuildTopologyManager(const AstlConfiguration&             configuration,
                                        std::optional<std::filesystem::path> cache_dir_path)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code>;
}  // namespace astl

#endif  // TOPOLOGY_BUILDER_
