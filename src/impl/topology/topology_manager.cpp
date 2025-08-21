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

#include <algorithm>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "astl_utils.hpp"
#include "config/scmi_specification_json.hpp"
#include "metric/metric_manager.hpp"

using json = nlohmann::json;

namespace astl {

auto TopologyManager::ScanForTargets() -> astl_status_code {
  _targets.clear();
  /// @todo ASTL-144 Actually implement first topology manager plugin
  _targets.push_back(std::make_unique<astl::Target>("AP0", "The SCMI interface on Socket0",
                                                    CollectorType::SCMI));  // This is a fake target placeholder
  return ASTL_STATUS_SUCCESS;
}

const std::vector<std::unique_ptr<ITarget>>& TopologyManager::GetTargets() const { return _targets; }

astl_status_code TopologyManager::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) {
  _targets = std::move(new_targets);
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
