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
#include "collector/collector_builder.hpp"
#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"
#include "common/capabilities.hpp"
#include "config/astl_configuration.hpp"
#include "metric/metric_builder.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_builder.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"
#include "topology/topology_builder.hpp"
#include "topology/topology_manager.hpp"

/** @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc.
 */
auto BuildOrchestrator(const astl::AstlConfiguration& configuration) -> astl_status_code {
  // TODO(ASTL-237): Deserialize from file here once we support loading .astl files.
  auto topology_manager = astl::BuildTopologyManager(configuration);
  if (!topology_manager) {
    return topology_manager.error();
  }

  auto collector_manager = astl::BuildCollectorManager(topology_manager.value()->GetTargets(), configuration);
  if (!collector_manager) {
    return collector_manager.error();
  }

  auto metric_manager = astl::BuildMetricManager(topology_manager.value()->GetTargets(), configuration);
  if (!metric_manager) {
    return metric_manager.error();
  }

  auto output_manager = astl::BuildOutputManager();
  if (!output_manager) {
    return output_manager.error();
  }

  // wire it all up in our new Orchestrator and replace the global instance with it.
  // Note, Orchestrator destructor should shut down all collection, etc.
  astl::Orchestrator::InitializeInstance(std::move(topology_manager.value()), std::move(collector_manager.value()),
                                         std::move(metric_manager.value()), std::move(output_manager.value()));
  // the orchestrator owns targets
  return ASTL_STATUS_SUCCESS;
}
