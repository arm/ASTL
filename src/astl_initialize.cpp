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

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_file_interface.hpp"
#include "astl_impl.hpp"
#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#include "common/capabilities.hpp"
#include "config/configuration_manager.hpp"
#include "metric/metric_manager.hpp"
#include "target.hpp"
#include "topology/topology_manager.hpp"

/** @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc.
 *  @todo https://jira.arm.com/browse/ASTL-131 clean up the dependency entanglement between the various managers.
 */
ASTL_API astl_status_code astlInitialize(const astl_initialization_parameters_t* init_params) {
  if (!init_params) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (init_params->_size != sizeof(astl_initialization_parameters_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }
  auto configuration = astl::ConfigurationManager::GetConfiguration(init_params);
  if (!configuration) {
    return configuration.error();
  }

  auto topology_manager = std::make_unique<astl::TopologyManager>();
  topology_manager->ScanForTargets();

  auto collector_manager =
      std::make_unique<astl::CollectorManager>(topology_manager->GetTargets(), configuration.value());
  // auto [targets, collector_manager] = topology_manager->InitializeCollectorManager();
  auto metric_manager_init_result = topology_manager->InitializeMetricManager(configuration.value());
  if (!metric_manager_init_result) {
    return metric_manager_init_result.error();
  }
  auto& metric_manager = *metric_manager_init_result;

  // wire it all up in our new Orchestrator and replace the global instance with it.
  // Note, Orchestrator destructor should shut down all collection, etc.
  astl::Orchestrator::InitializeInstance(std::move(topology_manager), std::move(collector_manager),
                                         std::move(metric_manager));
  // the orchestrator owns targets
  return ASTL_STATUS_SUCCESS;
}
