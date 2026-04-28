// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <expected>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_group_json_declaration.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"
#include "libsensors/libsensors_metric_builder.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/metric_manager.hpp"
#include "metric/procfs_metric_builder.hpp"
#include "target.hpp"

namespace astl {

namespace fs = std::filesystem;

static auto BuildDefaultCounterDescription(std::string_view metric_name) -> std::string {
  return "Underlying counter for " + std::string{metric_name};
}

static auto ReplaceAll(std::string value, std::string_view needle, std::string_view replacement) -> std::string {
  std::size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    value.replace(position, needle.size(), replacement);
    position += replacement.size();
  }
  return value;
}

static auto GetScmiTelemetrySubdirectory(const ITarget& target) -> std::string_view {
  if (const auto collector_target_path = target.CollectorTargetPath();
      collector_target_path.has_value() && !collector_target_path->empty()) {
    return *collector_target_path;
  }
  ASTL_LOG_WARNING(
      "SCMI target '{}' is missing collector path metadata; '{{telemetry_subdirectory}}' resolves to "
      "an empty string",
      target.Name());
  return {};
}

static auto ResolveScmiTargetNameTemplate(std::string_view name_template, const ITarget& target) -> std::string {
  auto resolved_name = std::string{name_template};
  resolved_name      = ReplaceAll(std::move(resolved_name), "{target_name}", target.Name());
  resolved_name =
      ReplaceAll(std::move(resolved_name), "{telemetry_subdirectory}", GetScmiTelemetrySubdirectory(target));
  return resolved_name;
}

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

using MetricGroupDescriptionMap = MetricManager::MetricGroupDescriptionMap;

static auto GetScmiDataEventDirectoryPath(const AstlConfiguration& configuration, const ITarget& target,
                                          ScmiDataEventId data_event_id, bool wide_hex) -> std::filesystem::path {
  const auto telemetry_subdirectory = GetScmiTelemetrySubdirectory(target);
  const auto folder_name = wide_hex ? std::format("0x{:08X}", data_event_id) : std::format("0x{:04X}", data_event_id);
  return configuration.scmi_sysfs_telemetry_root_path / telemetry_subdirectory / "des" / folder_name;
}

static auto FilterUnavailableScmiMetricConfigs(const AstlConfiguration& configuration, MetricConfigOnTargets& configs)
    -> void {
  std::erase_if(configs, [&configuration](auto& config_entry) {
    const auto& metric_config     = config_entry.first;
    auto&       targets           = config_entry.second;
    const auto* operation_builder = std::get_if<ScmiOperationBuilder>(&metric_config->GetOperationBuilder());

    std::erase_if(targets, [&](const ITarget* target) {
      if (target == nullptr || target->GetCollectorType() != CollectorType::SCMI) {
        return false;
      }
      if (operation_builder == nullptr) {
        return false;
      }

      const auto data_event_id = operation_builder->GetDataEventId();

      // Support both real-kernel 32-bit DE directory naming (0xXXXXXXXX) and
      // legacy/mocksysfs 16-bit naming (0xXXXX).
      const auto data_event_dir_path_wide = GetScmiDataEventDirectoryPath(configuration, *target, data_event_id, true);
      const auto data_event_dir_path_narrow =
          GetScmiDataEventDirectoryPath(configuration, *target, data_event_id, false);
      std::error_code ec{};
      const bool      wide_exists = std::filesystem::exists(data_event_dir_path_wide, ec);
      ec.clear();
      const bool narrow_exists = std::filesystem::exists(data_event_dir_path_narrow, ec);
      if (wide_exists || narrow_exists) {
        return false;
      }

      ASTL_LOG_WARNING(
          "Skipping SCMI metric '{}' (id: '{}') on target '{}' because DE directory '{}' (or legacy '{}') is missing",
          metric_config->Name(), metric_config->Id(), target->Name(), data_event_dir_path_wide.string(),
          data_event_dir_path_narrow.string());
      return true;
    });

    return targets.empty();
  });
}

static auto LoadMetricGroupDescriptions(const AstlConfiguration& configuration)
    -> std::expected<MetricGroupDescriptionMap, astl_status_code> {
  const auto metric_group_catalog_path = configuration.groups_dir_path / "metric_groups.json";
  if (!std::filesystem::exists(metric_group_catalog_path)) {
    ASTL_LOG_WARNING(
        "Metric group metadata config not found at {}. "
        "Metrics declaring metric_groups will fail during registration.",
        metric_group_catalog_path.string());
    return MetricGroupDescriptionMap{};
  }

  auto declaration =
      config::TryParseJsonFile<metrics::groups::spec::MetricGroupsDeclaration>(metric_group_catalog_path);
  if (!declaration.has_value()) {
    return std::unexpected(declaration.error());
  }

  MetricGroupDescriptionMap descriptions;
  descriptions.reserve(declaration->metric_groups.size());
  for (const auto& [group_name, group_declaration] : declaration->metric_groups) {
    descriptions.emplace(group_name, group_declaration.description);
  }
  return descriptions;
}

/**
 * @brief helper function to create MetricConfig objects for all SCMI metrics defined in the
 *        given SCMI specification and matching the given metric declaration from the top-level config file.
 */
static auto CreateScmiConfigurationsForMetrics(const AstlConfiguration&                 configuration,
                                               const scmi::spec::ScmiSpecification&     scmi_specification,
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
      FilterUnavailableScmiMetricConfigs(configuration, metric_configs_result.value());
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
static auto CreateScmiConfigurationsForCounters(const AstlConfiguration&                 configuration,
                                                const scmi::spec::ScmiSpecification&     scmi_specification,
                                                const metrics::spec::MetricsDeclaration& metric_declarations,
                                                const std::vector<const ITarget*>&       applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets configurations_on_targets;
  std::set<std::string> processed_counter_ids;  // ensure we don't repeat counters, even if used in multiple metrics
  // create counter MetricConfig objects for each underlying counter in the SCMI spec
  for (const auto& [metric_name, metric_declaration] : metric_declarations.metrics) {
    auto metric_registers = scmi::spec::GetMetricRegistersScmiData(metric_declaration, scmi_specification);
    for (const auto& register_declaration : metric_registers) {
      const std::string counter_id   = register_declaration.GetFullyQualifiedName();
      const std::string counter_name = register_declaration.name;
      if (processed_counter_ids.contains(counter_id)) {
        // already processed this counter, skip it
        continue;
      }
      processed_counter_ids.insert(counter_id);
      std::string description = metric_declaration.description.empty() ? BuildDefaultCounterDescription(metric_name)
                                                                       : metric_declaration.description;
      // Normalize SCMI base10 metadata into formula-space so collectors remain raw-only.
      auto scaling_formula =
          metrics::spec::BuildScalingFormulaFromBase10Modifier(register_declaration.base10_unit_modifier);
      // SCMI raw payload values are always uint64.
      constexpr astl_value_type_t input_value_type = ASTL_VALUE_UINT64;
      // Keep counter ValueType aligned with on-wire samples; scaling remains in formula-space only.
      const astl_value_type_t value_type = input_value_type;

      auto new_counter_config = std::make_unique<MetricConfig>(
          counter_name, std::move(description), ASTL_UNITS_UNKNOWN, value_type, ASTL_METRIC_IDENTIFIER_UNKNOWN,
          ASTL_METRIC_VALUE, CollectorType::SCMI, ScmiOperationBuilder{register_declaration.de_id},
          std::move(scaling_formula), input_value_type, std::vector<std::string>{}, counter_id);

      configurations_on_targets.emplace(std::move(new_counter_config), applicable_targets);
    }
  }
  FilterUnavailableScmiMetricConfigs(configuration, configurations_on_targets);
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

static auto ApplyConfiguredNameForScmiTarget(Target&                              concrete_target,
                                             const metrics::spec::PlatformLookup& platform_lookup) -> astl_status_code {
  auto uuid = GetUuidFromTarget(&concrete_target);
  if (!uuid.has_value()) {
    return ASTL_STATUS_SUCCESS;
  }

  auto metric_file_element = FindMetricsFileElementByUuid(platform_lookup, *uuid);
  if (!metric_file_element.has_value() || !metric_file_element->name.has_value()) {
    return ASTL_STATUS_SUCCESS;
  }

  const auto configured_name = ResolveScmiTargetNameTemplate(*metric_file_element->name, concrete_target);
  if (configured_name.empty()) {
    ASTL_LOG_ERROR("Configured SCMI target name resolved to an empty string for UUID {}", uuid->normalized_value);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  concrete_target.SetName(configured_name);
  return ASTL_STATUS_SUCCESS;
}

static auto ValidateUniqueTargetNames(const std::vector<std::unique_ptr<ITarget>>& targets) -> astl_status_code {
  std::unordered_set<std::string> seen_names;
  for (const auto& target_ptr : targets) {
    const auto insert_result = seen_names.emplace(target_ptr->Name());
    if (!insert_result.second) {
      ASTL_LOG_ERROR("Configured target names must be unique; duplicate target name '{}'", target_ptr->Name());
      return ASTL_STATUS_BAD_CONFIGURATION;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

struct ScmiSpecificationLookupContext {
  const scmi::spec::RepoMeta*          repo_meta;
  const metrics::spec::PlatformLookup* platform_lookup;
  const std::filesystem::path*         scmi_specification_dir;
  const std::filesystem::path*         metrics_declaration_dir;
};

static auto ApplyConfiguredScmiTargetNames(const AstlConfiguration&                     configuration,
                                           const std::vector<std::unique_ptr<ITarget>>& targets) -> astl_status_code {
  const auto has_scmi_targets = std::ranges::any_of(
      targets, [](const auto& target_ptr) { return target_ptr->GetCollectorType() == CollectorType::SCMI; });
  if (!has_scmi_targets) {
    return ASTL_STATUS_SUCCESS;
  }

  const auto& platform_lookup_json_path = configuration.metrics_dir_path / "platform_lookup.json";
  auto        platform_lookup = config::TryParseJsonFile<metrics::spec::PlatformLookup>(platform_lookup_json_path);
  if (!platform_lookup.has_value()) {
    return platform_lookup.error();
  }

  astl_status_code status = ASTL_STATUS_SUCCESS;
  for (const auto& target_ptr : targets) {
    if (target_ptr->GetCollectorType() != CollectorType::SCMI) {
      continue;
    }

    auto* concrete_target = dynamic_cast<Target*>(target_ptr.get());
    if (!concrete_target) {
      ASTL_LOG_WARNING("SCMI target {} cannot be cast to concrete Target type", target_ptr->Name());
      continue;
    }
    status = ApplyConfiguredNameForScmiTarget(*concrete_target, *platform_lookup);
    if (status != ASTL_STATUS_SUCCESS) {
      break;
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    status = ValidateUniqueTargetNames(targets);
  }
  return status;
}

static auto AddScmiSpecificationInfoForTarget(std::vector<ScmiUuidSpecificationInfo>& platform_specifications,
                                              const ITarget*                          target,
                                              const ScmiSpecificationLookupContext&   lookup_context) -> void {
  auto uuid_result = GetUuidFromTarget(target);
  if (!uuid_result.has_value()) {
    return;
  }
  const auto uuid = *uuid_result;

  auto existing_entry = std::find_if(platform_specifications.begin(), platform_specifications.end(),
                                     [&](const ScmiUuidSpecificationInfo& info) { return info.uuid == uuid; });
  if (existing_entry != platform_specifications.end()) {
    existing_entry->applicable_targets.push_back(target);
    return;
  }

  auto spec_file = FindSpecFileByUuid(*lookup_context.repo_meta, uuid);
  if (!spec_file) {
    ASTL_LOG_WARNING("No SCMI specification found for UUID {}", uuid.normalized_value);
    return;
  }

  auto metric_file_element = FindMetricsFileElementByUuid(*lookup_context.platform_lookup, uuid);
  if (!metric_file_element) {
    ASTL_LOG_WARNING("No metrics declaration found for UUID {}", uuid.normalized_value);
    return;
  }

  platform_specifications.emplace_back(ScmiUuidSpecificationInfo{
      .uuid                    = uuid,
      .specification_file      = *lookup_context.scmi_specification_dir / spec_file->specification_file,
      .metric_declaration_file = *lookup_context.metrics_declaration_dir / metric_file_element->metrics_file,
      .applicable_targets      = {target}});
}

/**
 * @brief Arrange the given targets by their UUIDs, and look up the relevant specification and metric declaration files.
 */
static auto LookUpSpecificationFiles(const AstlConfiguration&           configuration,
                                     const std::vector<const ITarget*>& scmi_targets)
    -> std::expected<std::vector<ScmiUuidSpecificationInfo>, astl_status_code> {
  // parse the 'repometa' json file that maps UUIDs to SCMI specification file paths
  const auto& scmi_specification_dir = configuration.scmi_specification_dir;
  const auto& repometa_json_path     = scmi_specification_dir / "repometa.json";
  auto        repo_meta              = config::TryParseJsonFile<scmi::spec::RepoMeta>(repometa_json_path);
  if (!repo_meta.has_value()) {
    return std::unexpected(repo_meta.error());
  }

  // parse the 'platform_lookup' json file that maps UUIDs to metric declaration file paths
  const auto& metrics_declaration_dir   = configuration.metrics_dir_path;
  const auto& platform_lookup_json_path = metrics_declaration_dir / "platform_lookup.json";
  auto        platform_lookup = config::TryParseJsonFile<metrics::spec::PlatformLookup>(platform_lookup_json_path);
  if (!platform_lookup.has_value()) {
    return std::unexpected(platform_lookup.error());
  }

  std::vector<ScmiUuidSpecificationInfo> platform_specifications;
  platform_specifications.reserve(scmi_targets.size());
  const ScmiSpecificationLookupContext lookup_context{
      .repo_meta               = &(*repo_meta),
      .platform_lookup         = &(*platform_lookup),
      .scmi_specification_dir  = &scmi_specification_dir,
      .metrics_declaration_dir = &metrics_declaration_dir,
  };

  for (const auto* target : scmi_targets) {
    AddScmiSpecificationInfoForTarget(platform_specifications, target, lookup_context);
  }

  return platform_specifications;
}

static auto AppendScmiConfigurationsForSpecification(const AstlConfiguration&         configuration,
                                                     const ScmiUuidSpecificationInfo& spec_info,
                                                     MetricAndCounterConfigurations&  metric_and_counter_configurations)
    -> astl_status_code {
  const std::string_view uuid_sv = spec_info.uuid.normalized_value;
  ASTL_LOG_DEBUG("Processing SCMI specification for UUID {}", uuid_sv);

  astl_status_code status = ASTL_STATUS_SUCCESS;
  auto             scmi_specification_result =
      config::TryParseJsonFile<scmi::spec::ScmiSpecification>(spec_info.specification_file);
  if (!scmi_specification_result.has_value()) {
    ASTL_LOG_ERROR("Failed to parse SCMI specification file {} for UUID {}: error code {}",
                   spec_info.specification_file.string(), uuid_sv, astlStatusString(scmi_specification_result.error()));
    status = scmi_specification_result.error();
  }

  std::optional<scmi::spec::ScmiSpecification> scmi_specification;
  if (status == ASTL_STATUS_SUCCESS) {
    scmi_specification = std::move(scmi_specification_result.value());
  }

  std::optional<metrics::spec::MetricsDeclaration> metric_declarations;
  if (status == ASTL_STATUS_SUCCESS) {
    auto metric_declaration_result =
        config::TryParseJsonFile<metrics::spec::MetricsDeclaration>(spec_info.metric_declaration_file);
    if (!metric_declaration_result.has_value()) {
      ASTL_LOG_ERROR("Failed to parse metric declaration file {} for UUID {}: error code {}",
                     spec_info.metric_declaration_file.string(), uuid_sv,
                     astlStatusString(metric_declaration_result.error()));
      status = metric_declaration_result.error();
    } else {
      metric_declarations = std::move(metric_declaration_result.value());
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    auto metric_configs_result = CreateScmiConfigurationsForMetrics(configuration, *scmi_specification,
                                                                    *metric_declarations, spec_info.applicable_targets);
    if (!metric_configs_result.has_value()) {
      status = metric_configs_result.error();
    } else {
      metric_and_counter_configurations.metric_configurations.merge(std::move(metric_configs_result.value()));
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    auto counter_configs_result = CreateScmiConfigurationsForCounters(
        configuration, *scmi_specification, *metric_declarations, spec_info.applicable_targets);
    if (!counter_configs_result.has_value()) {
      status = counter_configs_result.error();
    } else {
      metric_and_counter_configurations.counter_configurations.merge(std::move(counter_configs_result.value()));
    }
  }
  return status;
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
    const auto append_status =
        AppendScmiConfigurationsForSpecification(configuration, spec_info, metric_and_counter_configurations);
    if (append_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(append_status);
    }
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

  auto rename_status = ApplyConfiguredScmiTargetNames(configuration, targets);
  if (rename_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(rename_status);
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

  auto metric_group_descriptions = LoadMetricGroupDescriptions(configuration);
  if (!metric_group_descriptions.has_value()) {
    return std::unexpected(metric_group_descriptions.error());
  }

  std::unique_ptr<astl::IMetricManager> metric_manager =
      std::make_unique<astl::MetricManager>(capabilities, std::move(metric_group_descriptions.value()));

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
  status = RegisterProcfsMetrics(configuration, collector_type_to_targets_map, metric_manager.get());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  status = RegisterProcfsCounters(configuration, collector_type_to_targets_map, metric_manager.get());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  return metric_manager;
}

}  // namespace astl
