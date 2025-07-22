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

#include "topology/topology_manager.hpp"

#include <vector>

#include "astl_file_interface.hpp"
#include "collector/collector_manager.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#include "config/static_metric_config.hpp"
#include "metric/metric_manager.hpp"

namespace astl {

TopologyManager::TopologyManager(const AstlConfiguration& configuration) : _configuration(configuration) {}

// Initialize the CollectorManager based on the configuration
auto TopologyManager::InitializeCollectorManager() const
    -> std::pair<std::vector<std::unique_ptr<ITarget>>, std::unique_ptr<ICollectorManager>> {
  // TODO(ASTL-39) - add topologymanager. For now, hard-code one target
  // TODO(ASTL-40) - use configuration and platform config json to determine available collectors
  std::unique_ptr<astl::ITarget> target = std::make_unique<astl::Target>("Scmi0", "The SCMI interface on Socket0");
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));

  // tell collectorManager which collectors are suitable for which targets
  std::unordered_map<astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> target_to_collectors;
  // Set up the Scmi Sysfs Collector
  astl::FileInterface scmi_sysfs_file_interface{_configuration.scmi_sysfs_telemetry_root_path
                                                    ? *_configuration.scmi_sysfs_telemetry_root_path
                                                    : std::filesystem::path{"/tmp/fuse/scmi/scmi_telemetry"}};
  using ScmiCollector = astl::ScmiSysfsCollector<decltype(scmi_sysfs_file_interface)>;
  std::unique_ptr<astl::ICollector> scmi_collector =
      std::make_unique<ScmiCollector>(nullptr, std::move(scmi_sysfs_file_interface));
  std::vector<std::unique_ptr<astl::ICollector>> collectors_for_target;
  collectors_for_target.push_back(std::move(scmi_collector));
  target_to_collectors.emplace(targets[0].get(), std::move(collectors_for_target));

  return {std::move(targets), std::make_unique<astl::CollectorManager>(std::move(target_to_collectors))};
}

// Initialize the MetricManager based on the configuration and system config files
auto TopologyManager::InitializeMetricManager() const -> std::unique_ptr<IMetricManager> {
  // TODO(ASTL-40) - determine Metric configurations by using the configuration and system config files
  astl::CollectorCapability              collector_capabilities{astl::CollectorType::SCMI};
  astl::SystemCapability                 system_capabilities{};
  std::vector<astl::CollectorCapability> collector_caps_list{collector_capabilities};
  std::vector<astl::SystemCapability>    system_caps_list{system_capabilities};
  astl::Capabilities                     capabilities{std::move(collector_caps_list), std::move(system_caps_list)};

  std::unique_ptr<astl::IMetricManager> metric_manager = std::make_unique<astl::MetricManager>(capabilities);

  // Register all metrics from kMetricConfigs
  for (const auto& metric_config : kMetricConfigs) {
    auto metric = std::make_unique<astl::MetricConfig>(metric_config);
    if (auto finder = std::ranges::find(_configuration.metric_names_to_use, metric->Name());
        finder != _configuration.metric_names_to_use.end()) {
      metric_manager->RegisterMetric(std::move(metric));
    } else {
      // If the metric is disabled, do not register it
      ASTL_LOG_INFO("Metric {} is disabled by configuration", metric->Name());
    }
  }
  return metric_manager;
}

}  // namespace astl
