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

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "astl_utils.hpp"
#include "collector/collector_manager.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#include "config/scmi_specification_json.hpp"
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
    // @todo ASTL-40 default path for SCMI definition file)
    return configurations;
  }
  const auto& scmi_specification_path = configuration.scmi_specification_path.value();
  ASTL_LOG_DEBUG("Attmempting to parse {} for metric definitions", scmi_specification_path.string());
  try {
    std::ifstream json_file(scmi_specification_path);
    json          json_data          = json::parse(json_file);
    auto          specification_data = json_data.get<scmi::ScmiSpecification>();

    ASTL_LOG_DEBUG("specification_data.layout.members.size(): {}", specification_data.layout.members.size());

    // convert all of the metric declarations in the top-level config file into usable MetricConfig objects
    // based on the platform SCMI specification which includes the Data Event IDs.
    auto metric_config_maker = [&specification_data](const auto& name_and_declaration) {
      return CreateMetricConfig(name_and_declaration.first, name_and_declaration.second, specification_data.layout);
    };
    std::transform(configuration.metric_declarations.begin(), configuration.metric_declarations.end(),
                   std::back_inserter(configurations), metric_config_maker);

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

auto TopologyManager::ScanForTargets() -> astl_status_code {
  _targets.clear();
  /// @todo ASTL-144 Actually implement first topology manager plugin
  _targets.push_back(
      std::make_unique<astl::Target>("Scmi0", "The SCMI interface on Socket0"));  // This is a fake target placeholder
  return ASTL_STATUS_SUCCESS;
}

// Initialize the MetricManager based on the configuration and system config files
auto TopologyManager::InitializeMetricManagers(const AstlConfiguration& configuration) const
    -> std::expected<std::unordered_map<ITarget*, std::unique_ptr<IMetricManager>>, astl_status_code> {
  // @todo ASTL-40 - determine Metric configurations by using the configuration and system config files
  astl::CollectorCapability              collector_capabilities{astl::CollectorType::SCMI};
  astl::SystemCapability                 system_capabilities{};
  std::vector<astl::CollectorCapability> collector_caps_list{collector_capabilities};
  std::vector<astl::SystemCapability>    system_caps_list{system_capabilities};
  astl::Capabilities                     capabilities{std::move(collector_caps_list), std::move(system_caps_list)};

  std::unordered_map<ITarget*, std::unique_ptr<IMetricManager>> metric_manager_map;

  // @todo ASTL-127 - support multiple targets
  if (_targets.size() != 1) {
    ASTL_LOG_ERROR(
        "InitializeMetricManagers: Expected exactly one target (until ASTL issue #127 is resolved), found {}",
        _targets.size());
    return std::unexpected(ASTL_STATUS_NO_TARGETS_FOUND);
  }
  std::unique_ptr<astl::IMetricManager> metric_manager = std::make_unique<astl::MetricManager>(capabilities);

  // @todo ASTL-151 - Move InitializeMetricManager out of the topology manager
  auto metric_configurations = ParseMetricConfigurationsFromScmiSpecification(configuration);
  if (!metric_configurations) {
    return std::unexpected(metric_configurations.error());
  }
  for (auto& metric_config : metric_configurations.value()) {
    auto status = metric_manager->RegisterMetric(std::move(metric_config));
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
  }
  metric_manager_map.emplace(_targets[0].get(), std::move(metric_manager));
  return metric_manager_map;
}

const std::vector<std::unique_ptr<ITarget>>& TopologyManager::GetTargets() const { return _targets; }

astl_status_code TopologyManager::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) {
  _targets = std::move(new_targets);
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
