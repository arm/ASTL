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
#include "config/topology_manager.hpp"
#include "metric/metric_manager.hpp"
#include "target.hpp"

/* @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc.
 */
ASTL_API astl_status_code astlInitialize(const astl_initialization_parameters_t* init_params) {
  if (!init_params) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (init_params->_size != sizeof(astl_initialization_parameters_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }
  auto                  configuration = astl::ConfigurationManager::GetConfiguration();
  astl::TopologyManager topology_manager{configuration};
  auto [targets, collector_manager] = topology_manager.InitializeCollectorManager();
  auto metric_manager               = topology_manager.InitializeMetricManager();

  // wire it all up in our new Orchestrator and replace the global instance with it.
  // Note, Orchestrator destructor should shut down all collection, etc.
  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(collector_manager), std::move(metric_manager));
  // the orchestrator owns targets
  orchestrator->SetTargets(std::move(targets));
  // replace the existing orchestrator with the newly constructed one
  astl::Orchestrator::GetInstance() = std::move(orchestrator);
  return ASTL_STATUS_SUCCESS;
}
