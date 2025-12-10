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

#include "topology/topology_manager.hpp"

#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "astl/astl_errors.h"

using json = nlohmann::json;

namespace astl {

TopologyManager::TopologyManager(std::vector<std::unique_ptr<ITarget>>&& targets) : _targets{std::move(targets)} {}

auto TopologyManager::GetTargets() const -> const std::vector<std::unique_ptr<ITarget>>& { return _targets; }

auto TopologyManager::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code {
  _targets = std::move(new_targets);
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
