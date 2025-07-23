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

#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "astl_utils.hpp"
#include "collector/collector_manager.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#include "config/scmi_specification_json.hpp"
#include "config/static_metric_config.hpp"
#include "metric/metric_manager.hpp"

using json = nlohmann::json;

namespace astl {

/**
 * @brief helper function to parse a system scmi specification json file into MetricConfig objects
 */
auto ParseMetricConfigurationsFromScmiSpecification(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  std::vector<std::unique_ptr<MetricConfig>> configurations;
  if (!configuration.scmi_specification_path) {
    ASTL_LOG_INFO("No specification file path provided, so no metrics available from SCMI");
    // TODO(ASTL-40 - default path for SCMI definition file)
    return configurations;
  }
  const auto& scmi_specification_path = configuration.scmi_specification_path.value();
  ASTL_LOG_DEBUG("Attmempting to parse {} for metric definitions", scmi_specification_path.string());
  try {
    std::ifstream json_file(scmi_specification_path);
    json          json_data          = json::parse(json_file);
    auto          specification_data = json_data.get<scmi::ScmiSpecification>();

    ASTL_LOG_DEBUG("specification_data.datasources.size(): {}", specification_data.datasources.size());
    ASTL_LOG_DEBUG("specification_data.definitions.groups.size(): {}", specification_data.definitions.groups.size());
    ASTL_LOG_DEBUG("specification_data.layout.members.size(): {}", specification_data.layout.members.size());
    ASTL_LOG_DEBUG("specification_data.processes.size(): {}", specification_data.processes.size());
    ASTL_LOG_DEBUG("specification_data.transformations.size(): {}", specification_data.transformations.size());

    // TODO(ASTL-40 - replace this with Configmanager parsing specification file)
    configurations.push_back(std::make_unique<MetricConfig>(kTemperature));
  } catch (nlohmann::json::parse_error const& e) {
    ASTL_LOG_ERROR("Unable to parse SCMI definition file {}: {}", scmi_specification_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  } catch (nlohmann::json::type_error const& e) {
    ASTL_LOG_ERROR("Type error parsing SCMI definition file {}: {}", scmi_specification_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  } catch (nlohmann::json::exception const& e) {
    ASTL_LOG_ERROR("Exception caught while parsing SCMI definition file{}: {}", scmi_specification_path.string(),
                   e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return configurations;
}

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
auto TopologyManager::InitializeMetricManager() const
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  // TODO(ASTL-40) - determine Metric configurations by using the configuration and system config files
  astl::CollectorCapability              collector_capabilities{astl::CollectorType::SCMI};
  astl::SystemCapability                 system_capabilities{};
  std::vector<astl::CollectorCapability> collector_caps_list{collector_capabilities};
  std::vector<astl::SystemCapability>    system_caps_list{system_capabilities};
  astl::Capabilities                     capabilities{std::move(collector_caps_list), std::move(system_caps_list)};

  std::unique_ptr<astl::IMetricManager> metric_manager = std::make_unique<astl::MetricManager>(capabilities);

  auto metric_configurations = ParseMetricConfigurationsFromScmiSpecification(_configuration);
  if (!metric_configurations) {
    return std::unexpected(metric_configurations.error());
  }
  for (auto& metric_config : metric_configurations.value()) {
    auto status = metric_manager->RegisterMetric(std::move(metric_config));
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
  }
  return metric_manager;
}

const std::vector<std::unique_ptr<ITarget>>& TopologyManager::GetTargets() const { return _targets; }

astl_status_code TopologyManager::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) {
  _targets = std::move(new_targets);
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
