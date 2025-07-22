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

  /** @brief Initialize the CollectorManager based on the configuration */
  virtual auto InitializeCollectorManager() const
      -> std::pair<std::vector<std::unique_ptr<ITarget>>, std::unique_ptr<ICollectorManager>> = 0;

  /** @brief Initialize the MetricManager based on the configuration and system config files */
  virtual auto InitializeMetricManager() const -> std::unique_ptr<IMetricManager> = 0;

  virtual const std::vector<std::unique_ptr<ITarget>>& GetTargets() const = 0;

  /** @todo https://jira.arm.com/browse/ASTL-132
   *  Refactor: We probably want to provide a more controlled interface for modifying the target list
   *  For example, we could add member functions to enable/disable specific targets or
   *  modify the list internally when we read the configuration. */
  virtual astl_status_code SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) = 0;
};

}  // namespace astl

#endif  // I_TOPOLOGY_MANAGER_HPP_
