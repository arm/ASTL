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

#include "astl/astl_telemetry.h"
#include "astl_file_interface.hpp"
#include "astl_impl.hpp"
#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#include "common/capabilities.hpp"
#include "metric/metric_manager.hpp"
#include "target.hpp"

/* @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc.
 */
ASTL_API astl_status_code astlInitialize(const astl_initialization_parameters_t* init_params) {
  if (!init_params) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  // TODO(ASTL-39) - add topologymanager. For now, hard-code one target
  std::unique_ptr<astl::ITarget> target = std::make_unique<astl::Target>("Scmi0", "The SCMI interface on Socket0");

  // TODO(ASTL-40) - add configurationmanager to determine Metric configurations

  ////
  // Set up Collectors and CollectorManager (Ideally TopologyManager, and maybe ConfigManager should do this bit)
  ////
  // tell collectorManager which collectors are suitable for which targets
  std::unordered_map<astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> target_to_collectors;
  // Set up the Scmi Sysfs Collector
  // Note that details like ScmiCollector should normally be hidden from high-level setup code like this.
  // Normally, TopologyManager and/or ConfigurationManager would handle those details of collectors,
  // providing an abstract set of ICollectors, or just a fully-formed CollectorManager to link to Orchestrator.
  // But for an upcoming milestone, we're doing that directly here until TopologyManager is online.
  // TODO(ASTL-39) - hide deriving class details of collectors behind another initialization agent.
  astl::FileInterface scmi_sysfs_file_interface{std::filesystem::path{"scmi_telemetry"}};
  using ScmiCollector = astl::ScmiSysfsCollector<decltype(scmi_sysfs_file_interface)>;
  std::unique_ptr<astl::ICollector> scmi_collector =
      std::make_unique<ScmiCollector>(nullptr, std::move(scmi_sysfs_file_interface));
  std::vector<std::unique_ptr<astl::ICollector>> collectors_for_target;
  collectors_for_target.push_back(std::move(scmi_collector));
  target_to_collectors.emplace(target.get(), std::move(collectors_for_target));
  std::unique_ptr<astl::ICollectorManager> collector_manager =
      std::make_unique<astl::CollectorManager>(std::move(target_to_collectors));

  ////
  // set up Metrics and MetricManager (ideally ConfigManager should do this bit)
  ////
  astl::CollectorCapability              collector_capabilities{astl::CollectorType::SCMI};
  astl::SystemCapability                 system_capabilities{};
  std::vector<astl::CollectorCapability> collector_caps_list{collector_capabilities};
  std::vector<astl::SystemCapability>    system_caps_list{system_capabilities};
  astl::Capabilities                     capabilities{std::move(collector_caps_list), std::move(system_caps_list)};

  std::unique_ptr<astl::IMetricManager> metric_manager = std::make_unique<astl::MetricManager>(capabilities);
  // wire it all up in our new Orchestrator and replace the global instance with it.
  // Note, Orchestrator destructor should shut down all collection, etc.
  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(collector_manager), std::move(metric_manager));
  // add send the target into the Orchestrator
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));
  orchestrator->SetTargets(std::move(targets));
  // replace the existing orchestrator with the newly constructed one
  astl::Orchestrator::GetInstance() = std::move(orchestrator);
  return ASTL_STATUS_SUCCESS;
}
