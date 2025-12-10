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

#ifndef TOPOLOGY_MANAGER_HPP_
#define TOPOLOGY_MANAGER_HPP_

#include <memory>
#include <vector>

#include "target.hpp"
#include "topology/i_topology_manager.hpp"

namespace astl {

class TopologyManager : public ITopologyManager {
 public:
  TopologyManager() = delete;

  explicit TopologyManager(std::vector<std::unique_ptr<ITarget>>&&);

  auto GetTargets() const -> const std::vector<std::unique_ptr<ITarget>>& override;

  auto SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code override;

 private:
  std::vector<std::unique_ptr<ITarget>> _targets;
};

}  // namespace astl

#endif  // TOPOLOGY_MANAGER_HPP_
