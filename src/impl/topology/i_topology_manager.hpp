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

#ifndef I_TOPOLOGY_MANAGER_HPP_
#define I_TOPOLOGY_MANAGER_HPP_

#include <memory>
#include <utility>
#include <vector>

#include "collector/i_collector_manager.hpp"
#include "config/configuration_manager.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Interface for the Topology Manager.
 *
 * This interface defines the methods that a Topology Manager implements.
 * The interface classes are
 */
struct ITopologyManager {
  virtual ~ITopologyManager()                          = default;
  ITopologyManager()                                   = default;
  ITopologyManager(const ITopologyManager&)            = default;
  ITopologyManager& operator=(const ITopologyManager&) = default;
  ITopologyManager(ITopologyManager&&)                 = default;
  ITopologyManager& operator=(ITopologyManager&&)      = default;

  /** @brief Get the target list from the most recent call of ScanForTargets() */
  virtual auto GetTargets() const -> const std::vector<std::unique_ptr<ITarget>>& = 0;

  /** @todo ASTL-132
   *  Refactor: We probably want to provide a more controlled interface for modifying the target list
   *  For example, we could add member functions to enable/disable specific targets or
   *  modify the list internally when we read the configuration.
   *
   *  Ideally we would just call ScanForTargets() and never set the target list directly */
  virtual auto SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_TOPOLOGY_MANAGER_HPP_
