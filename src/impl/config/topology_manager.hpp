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
#include <utility>
#include <vector>

#include "collector/i_collector.hpp"
#include "collector/i_collector_manager.hpp"
#include "config/configuration_manager.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

namespace astl {

class TopologyManager {
 public:
  explicit TopologyManager(const AstlConfiguration& configuration);

  // Initialize the CollectorManager based on the configuration
  auto InitializeCollectorManager() const
      -> std::pair<std::vector<std::unique_ptr<ITarget>>, std::unique_ptr<ICollectorManager>>;

  // Initialize the MetricManager based on the configuration and system config files
  auto InitializeMetricManager() const -> std::unique_ptr<IMetricManager>;

 private:
  AstlConfiguration _configuration;
};

}  // namespace astl

#endif  // TOPOLOGY_MANAGER_HPP_
