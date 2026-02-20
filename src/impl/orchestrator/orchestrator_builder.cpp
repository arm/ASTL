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

#include <memory>

#include "astl/astl_errors.h"
#include "collector/collector_builder.hpp"
#include "config/astl_configuration.hpp"
#include "metric/metric_builder.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_builder.hpp"
#include "topology/topology_builder.hpp"

namespace fs = std::filesystem;

/** @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc.
 */
auto BuildOrchestrator(const astl::AstlConfiguration& configuration) -> astl_status_code {
  fs::path cache_dir_path = fs::temp_directory_path() / ("astl-" + std::to_string(std::time(nullptr)));
  if (configuration.load_file_path) {
    auto status = astl::Orchestrator::LoadFromFile(*configuration.load_file_path, cache_dir_path);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Orchestrator::GetInstance failed to load state from ASTL file '{}'",
                     configuration.load_file_path->string());
      return status;
    }
  }

  auto topology_manager = astl::BuildTopologyManager(configuration, cache_dir_path);
  if (!topology_manager) {
    return topology_manager.error();
  }

  // TODO(ASTL-279) - Once state machine is in place, we can skip building collector manager if loading from file
  auto collector_manager = astl::BuildCollectorManager(topology_manager.value()->GetTargets(), configuration);
  if (!collector_manager) {
    return collector_manager.error();
  }

  auto metric_manager = astl::BuildMetricManager(topology_manager.value()->GetTargets(), configuration, cache_dir_path);
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
                                         std::move(metric_manager.value()), std::move(output_manager.value()),
                                         cache_dir_path);
  // the orchestrator owns targets
  return ASTL_STATUS_SUCCESS;
}
