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

/**
 * @file output_builder.hpp
 * @brief Helper for constructing an `IOutputManager` based on runtime configuration.
 */
#ifndef OUTPUT_BUILDER_HPP_
#define OUTPUT_BUILDER_HPP_

#include <memory>

#include "output/i_output_manager.hpp"

namespace astl {

/**
 * @brief Construct and initialize an output manager instance.
 *
 * Reads global / per-target output configuration (if available) and creates the appropriate set of
 * output sinks. On success ownership of the manager is returned to the caller.
 *
 * Error Handling:
 *  - Returns `std::unexpected(astl_status_code)` if configuration parsing, allocation, or output
 *    instantiation fails.
 */
[[nodiscard]] auto BuildOutputManager() -> std::expected<std::unique_ptr<IOutputManager>, astl_status_code>;

}  // namespace astl

#endif  // COLLECTOR_BUILDER_HPP_
