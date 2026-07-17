// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/scmi_metric_builder.hpp"

#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "astl_logger.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"
#include "metric/scmi_metric_availability.hpp"
#include "metric/scmi_target_configuration.hpp"
#include "target.hpp"

namespace astl {

struct MetricAndCounterConfigurations {
  MetricConfigOnTargets metric_configurations;
  MetricConfigOnTargets counter_configurations;
};

static auto BuildDefaultCounterDescription(std::string_view metric_name) -> std::string {
  return "Underlying counter for " + std::string{metric_name};
}

static auto CreateScmiConfigurationsForMetrics(const AstlConfiguration&                 configuration,
                                               const scmi::spec::ScmiSpecification&     scmi_specification,
                                               const metrics::spec::MetricsDeclaration& metric_declarations,
                                               const std::vector<const ITarget*>&       applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets configurations;

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
      configurations.merge(metric_configs_result.value());
    } else {
      ASTL_LOG_ERROR("Failed to create metric config for '{}': error code {}", metric_name,
                     astlStatusString(metric_configs_result.error()));
    }
  }
  return configurations;
}

struct ScmiCounterConfigurationContext {
  std::reference_wrapper<const scmi::spec::ScmiSpecification> specification;
  std::reference_wrapper<std::set<std::string>>               processed_counter_id_target_pairs;
  std::reference_wrapper<MetricConfigOnTargets>               configurations_on_targets;
};

static auto AppendScmiCounterConfigurationsForTarget(std::string_view                            metric_name,
                                                     const metrics::spec::MetricJsonDeclaration& metric_declaration,
                                                     std::size_t target_index, const ITarget* target,
                                                     ScmiCounterConfigurationContext& context) -> void {
  auto metric_registers =
      scmi::spec::GetMetricRegistersScmiData(metric_declaration, context.specification.get(), target_index);
  for (const auto& register_declaration : metric_registers) {
    const std::string counter_id   = register_declaration.GetFullyQualifiedName();
    const std::string counter_name = register_declaration.name;
    const std::string dedup_key    = std::format("{}__{}", counter_id, GetStableTargetKey(*target));
    if (!context.processed_counter_id_target_pairs.get().insert(dedup_key).second) {
      continue;
    }
    std::string description = metric_declaration.description.empty() ? BuildDefaultCounterDescription(metric_name)
                                                                     : metric_declaration.description;
    auto        scaling_formula =
        metrics::spec::BuildScalingFormulaFromBase10Modifier(register_declaration.base10_unit_modifier);
    constexpr astl_value_type_t input_value_type = ASTL_VALUE_UINT64;
    const astl_value_type_t     value_type       = input_value_type;

    auto new_counter_config = std::make_unique<MetricConfig>(
        counter_name, std::move(description), ASTL_UNITS_UNKNOWN, value_type, ASTL_METRIC_IDENTIFIER_UNKNOWN,
        ASTL_METRIC_VALUE, CollectorType::SCMI, ScmiOperationBuilder{register_declaration.de_id},
        std::move(scaling_formula), input_value_type, std::vector<std::string>{}, counter_id);

    context.configurations_on_targets.get().emplace(std::move(new_counter_config), std::vector<const ITarget*>{target});
  }
}

static auto CreateScmiConfigurationsForCounters(const AstlConfiguration&                 configuration,
                                                const scmi::spec::ScmiSpecification&     scmi_specification,
                                                const metrics::spec::MetricsDeclaration& metric_declarations,
                                                const std::vector<const ITarget*>&       applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets           configurations_on_targets;
  std::set<std::string>           processed_counter_id_target_pairs;
  ScmiCounterConfigurationContext context{
      .specification                     = scmi_specification,
      .processed_counter_id_target_pairs = processed_counter_id_target_pairs,
      .configurations_on_targets         = configurations_on_targets,
  };

  for (const auto& [metric_name, metric_declaration] : metric_declarations.metrics) {
    auto collector_type = metrics::spec::ParseCollectorType(metric_declaration);
    if (!collector_type || collector_type != CollectorType::SCMI) {
      continue;
    }
    for (std::size_t target_index = 0; target_index < applicable_targets.size(); ++target_index) {
      const auto* target = applicable_targets[target_index];
      AppendScmiCounterConfigurationsForTarget(metric_name, metric_declaration, target_index, target, context);
    }
  }
  FilterUnavailableScmiMetricConfigs(configuration, configurations_on_targets);
  return configurations_on_targets;
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

static auto ParseMetricConfigurationsFromScmiSpecification(const AstlConfiguration&           configuration,
                                                           const std::vector<const ITarget*>& scmi_targets)
    -> std::expected<MetricAndCounterConfigurations, astl_status_code> {
  const auto platform_specifications = LookUpScmiSpecificationFiles(configuration, scmi_targets);
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

using RegisterConfigurationFunction = astl_status_code (IMetricManager::*)(std::unique_ptr<MetricConfig>,
                                                                           const std::vector<const ITarget*>&);

static auto RegisterConfigurations(MetricConfigOnTargets& configurations, IMetricManager* metric_manager,
                                   RegisterConfigurationFunction register_configuration) -> astl_status_code {
  astl_status_code status = ASTL_STATUS_SUCCESS;
  while (!configurations.empty() && status == ASTL_STATUS_SUCCESS) {
    auto node = configurations.extract(configurations.begin());
    status    = (metric_manager->*register_configuration)(std::move(node.key()), node.mapped());
  }
  return status;
}

auto RegisterScmiMetrics(
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

  auto status = RegisterConfigurations(scmi_metric_configurations->metric_configurations, metric_manager,
                                       &IMetricManager::RegisterMetric);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  return RegisterConfigurations(scmi_metric_configurations->counter_configurations, metric_manager,
                                &IMetricManager::RegisterCounter);
}

}  // namespace astl
