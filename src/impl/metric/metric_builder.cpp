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
#include <vector>

#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"
#include "libsensors/libsensors_metric_builder.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/metric_manager.hpp"

namespace astl {

namespace fs = std::filesystem;

/** @brief Holds info specific to one UUID-identified target _type_ on this platform,
 * including all detected targets with a matching UUID */
struct ScmiUuidSpecificationInfo {
  scmi::spec::Uuid            uuid;
  std::filesystem::path       specification_file;
  std::filesystem::path       metric_declaration_file;
  std::vector<const ITarget*> applicable_targets;
};

/** @brief helper struct to hold counter and metric configurations
 *
 */
struct MetricAndCounterConfigurations {
  MetricConfigOnTargets metric_configurations;
  MetricConfigOnTargets counter_configurations;
};

/** @brief helper function template to parse a given path as a given json structure type
 *
 * @param SpecType - template param specifying the type to try and parse to
 * @param json_file_path - path to the json file to parse
 * @returns expected holding the parsed structure on success, or an error status code on failure
 *
 */
template <typename SpecType>
inline auto TryParseJson(std::filesystem::path const& json_file_path) -> std::expected<SpecType, astl_status_code> {
  try {
    std::ifstream json_file{json_file_path};
    json          json_data   = json::parse(json_file);
    auto          parsed_data = json_data.get<SpecType>();
    return parsed_data;
  } catch (std::ifstream::failure const& e) {
    ASTL_LOG_ERROR("Unable to open json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  } catch (nlohmann::json::exception const& e) {
    ASTL_LOG_ERROR("Unable to parse json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

/**
 * @brief helper function to create MetricConfig objects for all SCMI metrics defined in the
 *        given SCMI specification and matching the given metric declaration from the top-level config file.
 */
static auto CreateScmiConfigurationsForMetrics(const scmi::spec::ScmiSpecification&     scmi_specification,
                                               const metrics::spec::MetricsDeclaration& metric_declarations,
                                               const std::vector<const ITarget*>&       applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets configurations;

  // convert all of the metric declarations in the top-level config file into usable MetricConfig objects
  // based on the platform SCMI specification which includes the Data Event IDs.
  // here the 'metric_name' is more descriptive from the config file like 'Soc Power' and the
  // 'metric_declaration.register' holds the register name like 'ENERGY_COUNTER'
  for (const auto& [metric_name, metric_declaration] : metric_declarations.metrics) {
    auto collector_type = metrics::spec::ParseCollectorType(metric_declaration);
    if (!collector_type || collector_type != CollectorType::SCMI) {
      ASTL_LOG_TRACE("CreateScmiConfigurationsForMetrics ignoring collector type '{}' for metric {}",
                     metric_declaration.collection.protocol, metric_name);
      continue;
    }
    auto metric_configs_result =
        metrics::spec::CreateScmiMetricConfigs(metric_name, metric_declaration, scmi_specification, applicable_targets);
    if (metric_configs_result.has_value()) {
      // combine the results into the output map
      configurations.merge(metric_configs_result.value());
    } else {
      ASTL_LOG_ERROR("Failed to create metric config for '{}': error code {}", metric_name,
                     astlStatusString(metric_configs_result.error()));
    }
  }
  return configurations;
}

/**
 * @brief helper function to create MetricConfig objects for all SCMI counters defined in the
 *       given SCMI specification and underlying the given metric declaration from the top-level config file.
 */
static auto CreateScmiConfigurationsForCounters(const scmi::spec::ScmiSpecification&     scmi_specification,
                                                const metrics::spec::MetricsDeclaration& metric_declarations,
                                                const std::vector<const ITarget*>&       applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets configurations_on_targets;
  std::set<std::string> processed_counter_names;  // ensure we don't repeat counters, even if used in multiple metrics
  // create counter MetricConfig objects for each underlying counter in the SCMI spec])
  for (const auto& [metric_name, metric_declaration] : metric_declarations.metrics) {
    auto metric_registers = scmi::spec::GetMetricRegistersScmiData(metric_declaration, scmi_specification);
    for (const auto& register_declaration : metric_registers) {
      std::string counter_name = register_declaration.name + "_" + metric_name;
      if (processed_counter_names.contains(counter_name)) {
        // already processed this counter, skip it
        continue;
      }
      std::string description = "Underlying counter for " + metric_name;
      auto        new_counter_config =
          std::make_unique<MetricConfig>(std::move(counter_name), std::move(description), ASTL_UNITS_UNKNOWN,
                                         ASTL_VALUE_UNKNOWN, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
                                         CollectorType::SCMI, ScmiOperationBuilder{register_declaration.de_id});

      configurations_on_targets.emplace(std::move(new_counter_config), applicable_targets);
    }
  }
  // @todo(ASTL-236) add support for counters specified in astl configuration separate from metrics.
  return configurations_on_targets;
}

/**
 * @brief best-effort helper to get the normalized SCMI UUID from a target, returning nullopt if any step fails
 */
static auto GetUuidFromTarget(const ITarget* target) -> std::optional<scmi::spec::Uuid> {
  const auto* concrete_target = dynamic_cast<const Target*>(target);
  if (!concrete_target) {
    ASTL_LOG_WARNING("Target cannot be cast to concrete Target type");
    return std::nullopt;
  }
  auto uuid_opt = concrete_target->GetUuid();
  if (!uuid_opt.has_value() || uuid_opt->empty()) {
    ASTL_LOG_WARNING("Target {} has no UUID", target->Name());
    return std::nullopt;
  }
  const auto uuid_res = astl::scmi::spec::GetNormalizedUuid(uuid_opt.value());
  if (!uuid_res) {
    ASTL_LOG_WARNING("Target {} has invalid SCMI UUID {}: error code {}", target->Name(), uuid_opt.value(),
                     astlStatusString(uuid_res.error()));
    return std::nullopt;
  }
  const auto uuid = *uuid_res;
  return uuid;
}

/**
 * @brief Arrange the given targets by their UUIDs, and look up the relevant specification and metric declaration files.
 */
static auto LookUpSpecificationFiles(const AstlConfiguration& configuration, std::vector<const ITarget*> scmi_targets)
    -> std::expected<std::vector<ScmiUuidSpecificationInfo>, astl_status_code> {
  // parse the 'repometa' json file that maps UUIDs to SCMI specification file paths
  const auto& scmi_specification_dir = configuration.scmi_specification_dir;
  const auto& repometa_json_path     = scmi_specification_dir / "repometa.json";
  auto        repo_meta              = TryParseJson<scmi::spec::RepoMeta>(repometa_json_path);
  if (!repo_meta.has_value()) {
    return std::unexpected(repo_meta.error());
  }

  // parse the 'platform_lookup' json file that maps UUIDs to metric declaration file paths
  const auto& metrics_declaration_dir   = configuration.metrics_dir_path;
  const auto& platform_lookup_json_path = metrics_declaration_dir / "platform_lookup.json";
  auto        platform_lookup           = TryParseJson<metrics::spec::PlatformLookup>(platform_lookup_json_path);
  if (!platform_lookup.has_value()) {
    return std::unexpected(platform_lookup.error());
  }

  std::vector<ScmiUuidSpecificationInfo> platform_specifications;
  platform_specifications.reserve(scmi_targets.size());

  // for each target, get its UUID, and add it to the platform_specification_by_uuid map,
  // either as a new entry, or by adding to the existing entry's applicable_targets list.
  // also look up the spec and metric declaration files for each UUID.
  std::for_each(scmi_targets.begin(), scmi_targets.end(), [&](const ITarget* target) -> void {
    auto uuid_result = GetUuidFromTarget(target);
    if (!uuid_result.has_value()) {
      return;  // skip targets with no valid Scmi UUID
    }
    const auto uuid = *uuid_result;

    // Check if we already have an entry for this UUID
    // Note  that '==' for UUIDs only compares the most significant bytes as specified in the repometa
    auto existing_entry = std::find_if(platform_specifications.begin(), platform_specifications.end(),
                                       [&](const ScmiUuidSpecificationInfo& info) { return info.uuid == uuid; });
    if (existing_entry != platform_specifications.end()) {
      // UUID already exists, just add this target to the list
      existing_entry->applicable_targets.push_back(target);
      return;
    }

    // Look up specification file in repo_meta
    auto spec_file = FindSpecFileByUuid(*repo_meta, uuid);
    if (!spec_file) {
      ASTL_LOG_WARNING("No SCMI specification found for UUID {}", uuid.normalized_value);
      return;
    }

    // Look up metrics declaration file in platform_lookup
    auto metric_file_element = FindMetricsFileElementByUuid(*platform_lookup, uuid);
    if (!metric_file_element) {
      ASTL_LOG_WARNING("No metrics declaration found for UUID {}", uuid.normalized_value);
      return;
    }
    platform_specifications.emplace_back(ScmiUuidSpecificationInfo{
        .uuid                    = uuid,
        .specification_file      = scmi_specification_dir / spec_file->specification_file,
        .metric_declaration_file = metrics_declaration_dir / metric_file_element->metrics_file,
        .applicable_targets      = {target}});
  });

  return platform_specifications;
}

/** @brief helper function to parse target-specific scmi specification json files into MetricConfig objects
 *
 * @param configuration The overall ASTL configuration including the path to the SCMI specification files
 * @param scmi_targets A vector of ITarget pointers representing the detected targets in the system
 *
 */
static auto ParseMetricConfigurationsFromScmiSpecification(const AstlConfiguration&           configuration,
                                                           std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<MetricAndCounterConfigurations, astl_status_code> {
  // arrange the targets by UUID, and look up the relevant file paths for SCMI specification and metrics declarations
  const auto platform_specifications = LookUpSpecificationFiles(configuration, scmi_targets);
  if (!platform_specifications.has_value()) {
    return std::unexpected(platform_specifications.error());
  }

  MetricAndCounterConfigurations metric_and_counter_configurations;

  for (const auto& spec_info : platform_specifications.value()) {
    const std::string_view uuid_sv = spec_info.uuid.normalized_value;
    ASTL_LOG_DEBUG("Processing SCMI specification for UUID {}", uuid_sv);

    // parse the SCMI specification file for this UUID
    auto scmi_specification_result = TryParseJson<scmi::spec::ScmiSpecification>(spec_info.specification_file);
    if (!scmi_specification_result.has_value()) {
      ASTL_LOG_ERROR("Failed to parse SCMI specification file {} for UUID {}: error code {}",
                     spec_info.specification_file.string(), uuid_sv,
                     astlStatusString(scmi_specification_result.error()));
      return std::unexpected(scmi_specification_result.error());
    }
    const auto& scmi_specification = scmi_specification_result.value();

    // parse the metric declaration file for this UUID
    auto metric_declaration_result = TryParseJson<metrics::spec::MetricsDeclaration>(spec_info.metric_declaration_file);
    if (!metric_declaration_result.has_value()) {
      ASTL_LOG_ERROR("Failed to parse metric declaration file {} for UUID {}: error code {}",
                     spec_info.metric_declaration_file.string(), uuid_sv,
                     astlStatusString(metric_declaration_result.error()));
      return std::unexpected(metric_declaration_result.error());
    }
    const auto& metric_declarations = metric_declaration_result.value();
    // convert all of the metric declarations in the top-level config file into usable MetricConfig objects
    auto metric_configs_result =
        CreateScmiConfigurationsForMetrics(scmi_specification, metric_declarations, spec_info.applicable_targets);
    if (!metric_configs_result.has_value()) {
      return std::unexpected(metric_configs_result.error());
    }
    // after this, the metric_configurations will include all metrics from the current UUID
    metric_and_counter_configurations.metric_configurations.merge(std::move(metric_configs_result.value()));
    // now combine the counters underlying those metrics for the current UUID
    auto counter_configs_result =
        CreateScmiConfigurationsForCounters(scmi_specification, metric_declarations, spec_info.applicable_targets);
    if (!counter_configs_result.has_value()) {
      return std::unexpected(counter_configs_result.error());
    }
    metric_and_counter_configurations.counter_configurations.merge(std::move(counter_configs_result.value()));
  }
  return metric_and_counter_configurations;
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
  // Extract and move config and targets from the maps
  for (auto it = scmi_metric_configurations->metric_configurations.begin();
       it != scmi_metric_configurations->metric_configurations.end();) {
    auto node   = scmi_metric_configurations->metric_configurations.extract(it++);
    auto status = metric_manager->RegisterMetric(std::move(node.key()), std::move(node.mapped()));
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  for (auto it = scmi_metric_configurations->counter_configurations.begin();
       it != scmi_metric_configurations->counter_configurations.end();) {
    auto node   = scmi_metric_configurations->counter_configurations.extract(it++);
    auto status = metric_manager->RegisterCounter(std::move(node.key()), std::move(node.mapped()));
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

static auto BuildMetricManagerFromASTLFile(const std::vector<std::unique_ptr<ITarget>>& targets,
                                           const fs::path                               cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  ASTL_LOG_DEBUG("Loading MetricManager from cache at {}", cache_dir_path.string());
  const std::filesystem::path metric_manager_file_path = cache_dir_path / kMetricManagerFileName;

  if (!std::filesystem::is_directory(cache_dir_path)) {
    ASTL_LOG_ERROR("Invalid ASTL cache directory: {}", cache_dir_path.string());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::ifstream metric_file(metric_manager_file_path, std::ios::binary | std::ios::in);
  if (!metric_file) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto metric_manager = ProtobufSerDes::Deserialize<std::unique_ptr<IMetricManager>>(metric_file, targets);

  if (!metric_manager.has_value()) {
    return std::unexpected(metric_manager.error());
  }

  return metric_manager;
}

auto BuildMetricManager(const std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration,
                        std::optional<std::filesystem::path> cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  if (configuration.load_file_path.has_value()) {
    if (!cache_dir_path.has_value()) {
      ASTL_LOG_ERROR("Cache directory path must be provided when load_file_path is specified in configuration");
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return BuildMetricManagerFromASTLFile(targets, cache_dir_path.value());
  }

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
