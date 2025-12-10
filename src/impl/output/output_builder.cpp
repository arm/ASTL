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

#include <memory>

#include "output/output_manager.hpp"

namespace astl {

/**
 * @brief Builds outputs for the given targets based on the provided configuration.
 *
 * @param targets The list of targets for which outputs are to be built.
 * @param configuration The configuration containing parameters for output creation.
 * @return An initialized IOutputManager associating each target with its corresponding outputs, or an error code.
 */
auto BuildOutputManager() -> std::expected<std::unique_ptr<IOutputManager>, astl_status_code> {
  return std::make_unique<OutputManager>();
}

}  // namespace astl
