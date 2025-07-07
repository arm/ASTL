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

  // TODO(ASTL-118): wire C API astlConfigureMetricCollectionOnTarget() to
  // Orchestrator::ConfigureMetricCollectionOnTarget() Move this logic to ConfigurationManager and Orchestrator.

  auto available_metrics = metric_manager->GetAvailableMetrics();
  if (!available_metrics) {
    return ASTL_STATUS_NO_METRICS_FOUND;
  }

  auto operations_on_sample = metric_manager->GetRequiredOperations(available_metrics.value());
  if (!operations_on_sample) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  astl::CollectionOperations operations{.operationsBeforeStart{},
                                        .operationsAtStart{},
                                        .operationsOnSample{std::move(operations_on_sample.value())},
                                        .operationsAtStop{},
                                        .samplingInterval{},
                                        .requirements{astl::CollectorCapability{astl::CollectorType::SCMI}}};

  astl_collection_parameters_t collection_params{
      ._size              = sizeof(astl_collection_parameters_t),
      ._sampling_interval = 0,
      ._collection_mode   = ASTL_COLLECTION_MODE_SAMPLING,
      ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD,
  };

  // TODO(ASTL-118): this target lookup only exists to upport the ConfigureCollectionOnTarget call below, which should
  // be handled elsewhere
  auto* target = targets[0].get();

  astl_status_code status =
      collector_manager->ConfigureCollectionOnTarget(target, collection_params, std::move(operations));
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to configure collection on target: {}", astlStatusString(status));
    return status;
  }

  // wire it all up in our new Orchestrator and replace the global instance with it.
  // Note, Orchestrator destructor should shut down all collection, etc.
  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(collector_manager), std::move(metric_manager));
  // the orchestrator owns targets
  orchestrator->SetTargets(std::move(targets));
  // replace the existing orchestrator with the newly constructed one
  astl::Orchestrator::GetInstance() = std::move(orchestrator);
  return status;
}
