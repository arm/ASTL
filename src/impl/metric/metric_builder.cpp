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

#include <expected>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <vector>

#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"
#include "libsensors_metric_builder.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/metric_manager.hpp"

namespace astl {

/** @brief helper struct to hold counter and metric configurations
 *
 */
struct MetricAndCounterConfigurations {
  std::vector<std::unique_ptr<MetricConfig>> metric_configurations;
  std::vector<std::unique_ptr<MetricConfig>> counter_configurations;
};

/**
 * @brief helper function to create MetricConfig objects for all SCMI metrics defined in the
 *        given SCMI specification and matching the given metric declaration from the top-level config file.
 */
static auto CreateScmiConfigurationsForMetrics(const AstlConfiguration&           configuration,
                                               const scmi::ScmiSpecification&     scmi_specification,
                                               std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  std::vector<std::unique_ptr<MetricConfig>> configurations;

  // convert all of the metric declarations in the top-level config file into usable MetricConfig objects
  // based on the platform SCMI specification which includes the Data Event IDs.
  // here the 'metric_name' is more descriptive from the config file like 'Soc Power' and the
  // 'metric_declaration.register' holds the register name like 'ENERGY_COUNTER'
  for (const auto& [metric_name, metric_declaration] : configuration.metric_declarations) {
    auto collector_type = ParseCollectorType(metric_declaration);
    if (!collector_type || collector_type != CollectorType::SCMI) {
      ASTL_LOG_TRACE("CreateScmiConfigurationsForMetrics ignoring collector type '{}' for metric {}",
                     metric_declaration.collection_protocol, metric_name);
      continue;
    }
    auto metric_configs_result =
        CreateScmiMetricConfigs(metric_name, metric_declaration, scmi_specification, scmi_targets);
    if (metric_configs_result.has_value()) {
      // move all the created MetricConfig objects into the output vector
      std::transform(metric_configs_result.value().begin(), metric_configs_result.value().end(),
                     std::back_inserter(configurations), [](auto& metric_config) { return std::move(metric_config); });
    } else {
      ASTL_LOG_ERROR("Failed to create metric config for '{}': error code {}", metric_name,
                     static_cast<int>(metric_configs_result.error()));
      // Continue processing other metrics instead of failing completely
    }
  }
  return configurations;
}

/**
 * @brief helper function to create MetricConfig objects for all SCMI counters defined in the
 *       given SCMI specification and underlying the given metric declaration from the top-level config file.
 */
static auto CreateScmiConfigurationsForCounters(const AstlConfiguration&       configuration,
                                                const scmi::ScmiSpecification& scmi_specification)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  std::vector<std::unique_ptr<MetricConfig>> configurations;
  std::set<std::string> processed_counter_names;  // ensure we don't repeat counters, even if used in multiple metrics
  for (const auto& [metric_name, metric_declaration] : configuration.metric_declarations) {
    auto metric_registers = scmi::GetMetricRegisters(metric_declaration.register_name, scmi_specification.layout);
    for (const auto& [reg_name, de_id] : metric_registers) {
      std::string counter_name = reg_name + "_" + metric_name;
      if (processed_counter_names.find(counter_name) != processed_counter_names.end()) {
        // already processed this counter, skip it
        continue;
      }
      std::string description = "Underlying counter for " + metric_name;
      configurations.emplace_back(std::make_unique<MetricConfig>(
          std::move(counter_name), std::move(description), ASTL_UNITS_UNKNOWN, ASTL_VALUE_UNKNOWN,
          ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, CollectorType::SCMI, ScmiOperationBuilder{de_id}));
    }
  }
  // @todo(ASTL-236) add support for counters specified in astl configuration separate from metrics.
  return configurations;
}

/** @brief helper function to parse a system scmi specification json file into MetricConfig objects
 *
 * @param configuration The overall ASTL configuration including the path to the SCMI specification file
 * @param scmi_targets A vector of ITarget pointers representing the detected targets in the system
 *
 */
static auto ParseMetricConfigurationsFromScmiSpecification(const AstlConfiguration&           configuration,
                                                           std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<MetricAndCounterConfigurations, astl_status_code> {
  if (!configuration.scmi_specification_path) {
    ASTL_LOG_INFO("No specification file path provided, so no metrics available from SCMI");
    // @todo ASTL-40 default path for SCMI definition file)
    return {};
  }
  const auto& scmi_specification_path = configuration.scmi_specification_path.value();
  ASTL_LOG_DEBUG("Attmempting to parse {} for metric definitions", scmi_specification_path.string());
  try {
    std::ifstream json_file(scmi_specification_path);
    json          json_data          = json::parse(json_file);
    auto          specification_data = json_data.get<scmi::ScmiSpecification>();
    ASTL_LOG_DEBUG("specification_data.layout.members.size(): {}", specification_data.layout.members.size());

    auto metric_configs_result  = CreateScmiConfigurationsForMetrics(configuration, specification_data, scmi_targets);
    auto counter_configs_result = CreateScmiConfigurationsForCounters(configuration, specification_data);
    return MetricAndCounterConfigurations{.metric_configurations  = std::move(metric_configs_result.value()),
                                          .counter_configurations = std::move(counter_configs_result.value())};
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
}

/**
 * @brief Scan the collector_type_to_targets_map for SCMI targets.
 *        Use the given configuration to create metrics and register them in the metric_manager.
 */
static auto RegisterScmiMetrics(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code {
  if (!metric_manager) {
    ASTL_LOG_ERROR("metric_manager is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto scmi_targets_iter = collector_type_to_targets_map.find(CollectorType::SCMI);
  if (scmi_targets_iter == collector_type_to_targets_map.end()) {
    ASTL_LOG_INFO("No targets with SCMI collector type found, skipping SCMI metric registration");
    return ASTL_STATUS_SUCCESS;
  }
  auto scmi_metric_configurations =
      ParseMetricConfigurationsFromScmiSpecification(configuration, scmi_targets_iter->second);
  if (!scmi_metric_configurations) {
    return scmi_metric_configurations.error();
  }

  for (auto& scmi_metric_config : scmi_metric_configurations->metric_configurations) {
    auto status = metric_manager->RegisterMetric(std::move(scmi_metric_config), scmi_targets_iter->second);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  for (auto& scmi_counter_config : scmi_metric_configurations->counter_configurations) {
    auto status = metric_manager->RegisterCounter(std::move(scmi_counter_config), scmi_targets_iter->second);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto BuildMetricManager(const std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  // arrange the targets by the collector type
  std::unordered_map<CollectorType, std::vector<const ITarget*>> collector_type_to_targets_map;
  for (const auto& target : targets) {
    collector_type_to_targets_map[target->GetCollectorType()].push_back(target.get());
  }
  // build a vector of CollectorCapability objects for each unique collector
  std::vector<astl::CollectorCapability> collector_caps_list;
  for (const auto& [collector_type, target_list] : collector_type_to_targets_map) {
    if (collector_type == CollectorType::UNKNOWN) {
      ASTL_LOG_ERROR("BuildMetricManager: Found target with UNKNOWN collector type, skipping");
      continue;
    }
    ASTL_LOG_DEBUG("BuildMetricManager: Found {} targets with collector type {}", target_list.size(),
                   static_cast<int>(collector_type));
    collector_caps_list.emplace_back(collector_type);
  }

  // create the astl::Capabilities object to pass to the MetricManager
  astl::SystemCapability              system_capabilities{};
  std::vector<astl::SystemCapability> system_caps_list{system_capabilities};
  astl::Capabilities                  capabilities{std::move(collector_caps_list), std::move(system_caps_list)};

  std::unique_ptr<astl::IMetricManager> metric_manager = std::make_unique<astl::MetricManager>(capabilities);

  // handle any SCMI metrics and targets
  auto status = RegisterScmiMetrics(configuration, collector_type_to_targets_map, metric_manager.get());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  // handle the libsensors  metrics remaining in the configuration
  status = RegisterLibsensorsMetrics(configuration, collector_type_to_targets_map, metric_manager.get());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  return metric_manager;
}

}  // namespace astl
