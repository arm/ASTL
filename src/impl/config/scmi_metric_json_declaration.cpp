// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/scmi_metric_json_declaration.hpp"

#include <cstdint>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_internal_status.hpp"
#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"
#include "metric/formula_builder.hpp"
#include "target.hpp"

using json = nlohmann::json;

namespace astl::metrics::spec {

auto ParseScmiMetricJsonCollectionSettings(const MetricJsonCollectionSettings& collection_setting)
    -> std::expected<ScmiMetricJsonCollectionSettings, astl_status_code> {
  if (ToLowerCopy(collection_setting.protocol) != "scmi") {
    ASTL_LOG_ERROR("SCMI collection parser received unsupported protocol '{}'", collection_setting.protocol);
    return std::unexpected(astl::kInternalNotImplemented);
  }

  ScmiMetricJsonCollectionSettings settings;
  if (collection_setting.raw_json.is_null()) {
    return settings;
  }
  if (!collection_setting.raw_json.is_object()) {
    ASTL_LOG_ERROR("SCMI collection settings must be a JSON object");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  if (collection_setting.raw_json.contains("register")) {
    collection_setting.raw_json.at("register").get_to(settings.register_name);
  }
  if (collection_setting.raw_json.contains("scmi_component_filter")) {
    collection_setting.raw_json.at("scmi_component_filter").get_to(settings.scmi_component_filter);
  }
  if (collection_setting.raw_json.contains("scmi_instance_filter")) {
    collection_setting.raw_json.at("scmi_instance_filter").get_to(settings.scmi_instance_filter);
  }
  return settings;
}

namespace {

auto ParseValueType() -> astl_value_type_t { return ASTL_VALUE_UINT64; }

auto ParseScmiOutputValueType(astl_value_type_t input_value_type, int32_t base10_unit_modifier) -> astl_value_type_t {
  if (input_value_type == ASTL_VALUE_UNKNOWN) {
    return ASTL_VALUE_UNKNOWN;
  }
  if (base10_unit_modifier != 0) {
    return ASTL_VALUE_FLOAT64;
  }
  return input_value_type;
}

auto BuildScmiMetricDescription(std::string_view metric_name, astl_metric_identifier_t identifier) -> std::string {
  switch (identifier) {
    case ASTL_METRIC_IDENTIFIER_TEMPERATURE:
      return std::string{"Temperature reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_ENERGY:
      return std::string{"Energy reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_POWER:
      return std::string{"Power reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_POWER_LIMIT:
      return std::string{"Power limit reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_POWER_THROTTLE:
      return std::string{"Power throttle reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_FREQUENCY:
      return std::string{"Frequency reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_VOLTAGE:
      return std::string{"Voltage reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_CURRENT:
      return std::string{"Current reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_BANDWIDTH:
      return std::string{"Bandwidth reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_FAN_SPEED:
      return std::string{"Fan speed reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_HUMIDITY:
      return std::string{"Humidity reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_STATUS:
      return std::string{"Status reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_THERMAL_LIMIT:
      return std::string{"Thermal limit reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_THERMAL_THROTTLE:
      return std::string{"Thermal throttle reading for "} + std::string{metric_name};
    case ASTL_METRIC_IDENTIFIER_COUNT:
      return std::string{"Counter reading for "} + std::string{metric_name};
    default:
      return std::string{"Reading for "} + std::string{metric_name};
  }
}

auto BuildScmiComponentInstanceSuffix(const scmi::spec::ScmiMetricDeclaration& scmi_metric_declaration) -> std::string {
  if (scmi_metric_declaration.component.empty()) {
    return {};
  }
  if (scmi_metric_declaration.instance.empty()) {
    return std::format(" [component: {}]", scmi_metric_declaration.component);
  }
  return std::format(" [component: {} instance: {}]", scmi_metric_declaration.component,
                     scmi_metric_declaration.instance);
}

auto ResolveScmiMetricDescription(const MetricJsonDeclaration& metric_declaration, std::string_view metric_name,
                                  astl_metric_identifier_t                 identifier,
                                  const scmi::spec::ScmiMetricDeclaration& scmi_metric_declaration) -> std::string {
  std::string base_description;
  if (!metric_declaration.description.empty()) {
    base_description = metric_declaration.description;
  } else {
    base_description = BuildScmiMetricDescription(metric_name, identifier);
  }
  return base_description + BuildScmiComponentInstanceSuffix(scmi_metric_declaration);
}

auto BuildScmiUniqueMetricName(std::string_view                         metric_name,
                               const scmi::spec::ScmiMetricDeclaration& scmi_metric_declaration) -> std::string {
  const auto component =
      scmi_metric_declaration.component.empty() ? std::string{"component"} : scmi_metric_declaration.component;
  const auto instance = scmi_metric_declaration.instance.empty() ? std::string{"0"} : scmi_metric_declaration.instance;
  return std::format("{}.{}.{}", component, instance, metric_name);
}

auto BuildScmiMetricId(std::string_view metric_name, const ITarget& target) -> std::string {
  return std::format("{}__{}", metric_name, GetStableTargetKey(target));
}

auto ParseJsonValueToAstlValue(const nlohmann::json& val, const std::string& label, std::string_view metric_key_name)
    -> std::expected<AstlValue, astl_status_code> {
  if (val.is_number_integer()) {
    return AstlValue{val.get<uint64_t>()};
  }
  if (val.is_number_float()) {
    return AstlValue{val.get<double>()};
  }
  if (val.is_boolean()) {
    return AstlValue{val.get<bool>()};
  }

  ASTL_LOG_ERROR("Unsupported JSON value type for label '{}' in finite_set_values (metric {})", label, metric_key_name);
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

auto CreateFiniteSetMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                                  scmi::spec::ScmiSpecification const& scmi_spec,
                                  std::vector<const ITarget*> const&   applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  if (!metric_declaration.finite_set_values.has_value()) {
    ASTL_LOG_ERROR("Finite set metric {} missing 'finite_set_values' configuration", metric_key_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for finite set metric {}", metric_declaration.collection.protocol,
                   metric_key_name);
    return std::unexpected(astl::kInternalNotImplemented);
  }
  auto scmi_collection = ParseScmiMetricJsonCollectionSettings(metric_declaration.collection);
  if (!scmi_collection.has_value()) {
    return std::unexpected(scmi_collection.error());
  }

  FiniteSetMetricConfig::FiniteSet      finite_set;
  FiniteSetMetricConfig::ValueToInfoMap value_to_info;

  for (const auto& [label, entry] : metric_declaration.finite_set_values.value()) {
    if (label.empty()) {
      ASTL_LOG_ERROR("Empty label in finite_set_values for metric {}", metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    if (!entry.is_object() || !entry.contains("value") || !entry.contains("description")) {
      ASTL_LOG_ERROR(
          "finite_set_values entry '{}' for metric {} must be an object with 'value' and 'description' fields", label,
          metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    auto parsed_value_result = ParseJsonValueToAstlValue(entry["value"], label, metric_key_name);
    if (!parsed_value_result) {
      return std::unexpected(parsed_value_result.error());
    }

    if (!finite_set.emplace(*parsed_value_result).second) {
      ASTL_LOG_ERROR("Duplicate finite set value for label '{}' in metric {}", label, metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    value_to_info[*parsed_value_result] =
        FiniteSetMetricConfig::StateInfo{label, entry["description"].get<std::string>()};
  }

  if (finite_set.empty()) {
    ASTL_LOG_ERROR("Empty finite set for metric {}", metric_key_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ASTL_LOG_INFO("Parsed finite set metric '{}' with {} labels and {} unique values", metric_key_name,
                value_to_info.size(), finite_set.size());

  MetricConfigOnTargets metric_configs_on_targets;
  const auto            identifier       = ParseMetricIdentifier(metric_declaration.identifier);
  const auto            input_value_type = ParseValueType();
  // Per-target loop: each tlm-N target gets globally-unique instance labels and DE ids
  // (GetMetricRegistersScmiData derives the DE id from the global instance index
  // target_index * count + local_instance).
  for (std::size_t target_index = 0; target_index < applicable_targets.size(); ++target_index) {
    const auto* target           = applicable_targets[target_index];
    auto        metric_registers = scmi::spec::GetMetricRegistersScmiData(metric_declaration, scmi_spec, target_index);
    if (target_index == 0 && metric_registers.empty()) {
      ASTL_LOG_ERROR("No Data Event IDs found for finite set metric {} (register '{}')", metric_key_name,
                     scmi_collection->register_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    for (const auto& scmi_metric_declaration : metric_registers) {
      const auto              units                = scmi_metric_declaration.units;
      const int32_t           base10_unit_modifier = scmi_metric_declaration.base10_unit_modifier;
      const astl_value_type_t value_type           = ParseScmiOutputValueType(input_value_type, base10_unit_modifier);
      const auto&             de_id                = scmi_metric_declaration.de_id;

      const auto           metric_name = BuildScmiUniqueMetricName(metric_key_name, scmi_metric_declaration);
      auto                 metric_id   = BuildScmiMetricId(metric_name, *target);
      ScmiOperationBuilder operation_builder{de_id};
      auto                 finite_set_copy = finite_set;
      auto                 info_copy       = value_to_info;

      auto formula_result = BuildFormula(metric_declaration.formula);
      if (!formula_result.has_value()) {
        return std::unexpected(formula_result.error());
      }
      auto composed_formula = ComposeFormulas(std::move(formula_result.value()),
                                              BuildScalingFormulaFromBase10Modifier(base10_unit_modifier));

      auto new_metric_config = std::make_unique<FiniteSetMetricConfig>(
          metric_name, metric_declaration.description, units, value_type, ASTL_METRIC_FINITE_SET_VALUE, identifier,
          collector_type.value(), std::move(operation_builder), std::move(finite_set_copy), std::move(info_copy),
          std::move(composed_formula), input_value_type, std::vector<std::string>{}, std::move(metric_id));

      metric_configs_on_targets.emplace(std::move(new_metric_config), std::vector<const ITarget*>{target});
    }
  }
  ASTL_LOG_INFO("Created {} finite set metric config(s) for '{}' with {} valid values",
                metric_configs_on_targets.size(), metric_key_name, finite_set.size());
  return metric_configs_on_targets;
}

auto GetResidencyMetricStateToInfoMapForInstance(
    std::string_view                                     metric_key_name,
    scmi::spec::ResidencyStateRegisterDefinitions const& matching_scmi_register_definitions,
    scmi::spec::InstanceId instance, MetricJsonDeclaration const& metric_declaration)
    -> std::expected<ResidencyMetricConfig::StateToInfoMap, astl_status_code> {
  ResidencyMetricConfig::StateToInfoMap state_to_info_map;

  for (const auto& [state_name, state_config] : metric_declaration.states.value()) {
    if (!state_config.contains("tick_frequency")) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} missing required 'tick_frequency' field", state_name,
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    if (!state_config.contains("description")) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} missing required 'description' field", state_name,
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    double      tick_frequency    = state_config["tick_frequency"].get<double>();
    std::string state_description = state_config["description"].get<std::string>();

    auto state_iter = matching_scmi_register_definitions.state_to_register_def.find(state_name);
    if (state_iter == matching_scmi_register_definitions.state_to_register_def.end()) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} not found in matching SCMI registers", state_name,
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    auto                 data_event_id = scmi::spec::GetDataEventId(state_iter->second.base_data_event_id, instance);
    ScmiOperationBuilder operation_builder{data_event_id};
    ResidencyMetricConfig::StateInfo state_info{state_name, state_description, tick_frequency,
                                                std::move(operation_builder)};
    state_to_info_map[state_name] = std::move(state_info);
  }
  return state_to_info_map;
}

auto CreateResidencyMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                                  scmi::spec::ScmiSpecification const& scmi_spec,
                                  std::vector<const ITarget*> const&   applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets metric_configs_on_targets;

  const auto identifier     = ParseMetricIdentifier(metric_declaration.identifier);
  const auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection.protocol,
                   metric_key_name);
    return std::unexpected(astl::kInternalNotImplemented);
  }
  auto scmi_collection = ParseScmiMetricJsonCollectionSettings(metric_declaration.collection);
  if (!scmi_collection.has_value()) {
    return std::unexpected(scmi_collection.error());
  }
  auto matching_scmi_register_definitions = FindMatchingScmiRegistersForResidency(metric_declaration, scmi_spec);
  if (!matching_scmi_register_definitions) {
    return std::unexpected(matching_scmi_register_definitions.error());
  }
  const auto value_type       = ParseValueType();
  const auto input_value_type = value_type;

  // Per-target loop so each tlm-N target gets DE ids referring to its own sysfs entries (local
  // instance index 0..count-1) while the published metric name uses the globally-unique label
  // (`target_index * count + local_instance`).
  const auto local_count = matching_scmi_register_definitions->count;
  for (std::size_t target_index = 0; target_index < applicable_targets.size(); ++target_index) {
    const auto* target = applicable_targets[target_index];
    for (scmi::spec::InstanceId local_instance = 0; local_instance < local_count; ++local_instance) {
      const std::size_t global_instance        = (target_index * local_count) + local_instance;
      const std::string global_instance_string = std::to_string(global_instance);
      if (scmi_collection->scmi_instance_filter.has_value() &&
          global_instance_string != scmi_collection->scmi_instance_filter.value()) {
        continue;
      }
      // For multi-target configurations the metric name always includes the instance suffix so
      // PSS.0..(N*count-1) remain unique across all targets.
      std::string name                     = (local_count > 1 || applicable_targets.size() > 1)
                                                 ? std::format("{}.{}", metric_key_name, global_instance_string)
                                                 : std::string{metric_key_name};
      auto        state_to_info_map_result = GetResidencyMetricStateToInfoMapForInstance(
          metric_key_name, *matching_scmi_register_definitions, local_instance, metric_declaration);
      if (!state_to_info_map_result.has_value()) {
        return std::unexpected(state_to_info_map_result.error());
      }

      auto formula_result = BuildFormula(metric_declaration.formula);
      if (!formula_result.has_value()) {
        return std::unexpected(formula_result.error());
      }

      auto new_config = std::make_unique<ResidencyMetricConfig>(
          name, metric_declaration.description, ParseUnits(metric_declaration.unit.value_or("")), value_type,
          ASTL_METRIC_RESIDENCY, identifier, collector_type.value(), std::move(state_to_info_map_result.value()),
          metric_declaration.inferred_state, std::move(formula_result.value()), input_value_type, name);

      metric_configs_on_targets.emplace(std::move(new_config), std::vector<const ITarget*>{target});
    }
  }
  return metric_configs_on_targets;
}

auto CreateBasicMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                              scmi::spec::ScmiSpecification const& scmi_spec, astl_metric_type_t metric_type,
                              std::vector<const ITarget*> const& applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection.protocol,
                   metric_key_name);
    return std::unexpected(astl::kInternalNotImplemented);
  }
  auto scmi_collection = ParseScmiMetricJsonCollectionSettings(metric_declaration.collection);
  if (!scmi_collection.has_value()) {
    return std::unexpected(scmi_collection.error());
  }
  MetricConfigOnTargets metric_configs_on_targets;
  const auto            identifier       = ParseMetricIdentifier(metric_declaration.identifier);
  const auto            input_value_type = ParseValueType();

  // Per-target loop: each tlm-N target produces globally-unique instance labels and DE ids
  // (GetMetricRegistersScmiData derives the DE id from the global instance index
  // target_index * count + local_instance) so that, e.g., PSS.0..2 appear on tlm-0 and PSS.3..5 on
  // tlm-1 when count=3.
  for (std::size_t target_index = 0; target_index < applicable_targets.size(); ++target_index) {
    const auto* target           = applicable_targets[target_index];
    auto        metric_registers = scmi::spec::GetMetricRegistersScmiData(metric_declaration, scmi_spec, target_index);
    if (target_index == 0 && metric_registers.empty()) {
      ASTL_LOG_INFO("No Data Event IDs found for metric {} (register '{}')", metric_key_name,
                    scmi_collection->register_name);
    }
    for (const auto& scmi_metric_declaration : metric_registers) {
      const auto              units                = scmi_metric_declaration.units;
      const int32_t           base10_unit_modifier = scmi_metric_declaration.base10_unit_modifier;
      const astl_value_type_t value_type           = ParseScmiOutputValueType(input_value_type, base10_unit_modifier);
      const auto              metric_name = BuildScmiUniqueMetricName(metric_key_name, scmi_metric_declaration);
      auto                    metric_id   = BuildScmiMetricId(metric_name, *target);
      ScmiOperationBuilder    operation_builder{scmi_metric_declaration.de_id};

      auto formula_result = BuildFormula(metric_declaration.formula);
      if (!formula_result.has_value()) {
        return std::unexpected(formula_result.error());
      }
      auto composed_formula = ComposeFormulas(std::move(formula_result.value()),
                                              BuildScalingFormulaFromBase10Modifier(base10_unit_modifier));

      auto metric_groups     = metric_declaration.metric_groups.value_or(std::vector<std::string>{});
      auto new_metric_config = std::make_unique<MetricConfig>(
          metric_name,
          ResolveScmiMetricDescription(metric_declaration, metric_key_name, identifier, scmi_metric_declaration), units,
          value_type, identifier, metric_type, collector_type.value(), std::move(operation_builder),
          std::move(composed_formula), input_value_type, std::move(metric_groups), std::move(metric_id));
      metric_configs_on_targets.emplace(std::move(new_metric_config), std::vector<const ITarget*>{target});
    }
  }
  return metric_configs_on_targets;
}

}  // namespace

auto BuildScalingFormulaFromBase10Modifier(int32_t base10_unit_modifier) -> AnyFormula {
  if (base10_unit_modifier == 0) {
    return AnyFormula{IdentityFormula{}};
  }
  constexpr int      chunk_exponent = 19;
  constexpr uint64_t chunk_literal  = 10000000000000000000ULL;
  constexpr uint64_t decimal_radix  = 10ULL;

  auto append_scale_step = [](AnyFormula formula, uint64_t numerator, uint64_t denominator) -> AnyFormula {
    return ComposeFormulas(std::move(formula), AnyFormula{
                                                   ScalingFormula{numerator, denominator}
    });
  };

  AnyFormula result = AnyFormula{IdentityFormula{}};
  int64_t    exponent{base10_unit_modifier};
  const bool is_positive = exponent > 0;
  if (!is_positive) {
    exponent = -exponent;
  }
  constexpr int64_t max_supported_exponent = 190;
  if (exponent > max_supported_exponent) {
    ASTL_LOG_WARNING("Clamping base10_unit_modifier {} to {} for scaling pipeline construction", base10_unit_modifier,
                     is_positive ? max_supported_exponent : -max_supported_exponent);
    exponent = max_supported_exponent;
  }

  while (exponent >= chunk_exponent) {
    result = is_positive ? append_scale_step(std::move(result), chunk_literal, 1)
                         : append_scale_step(std::move(result), 1, chunk_literal);
    exponent -= chunk_exponent;
  }
  if (exponent > 0) {
    uint64_t literal = 1;
    for (int i = 0; i < exponent; ++i) {
      literal *= decimal_radix;
    }
    result = is_positive ? append_scale_step(std::move(result), literal, 1)
                         : append_scale_step(std::move(result), 1, literal);
  }
  return result;
}

auto CreateScmiMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                             scmi::spec::ScmiSpecification const& scmi_spec,
                             std::vector<const ITarget*> const&   applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  auto metric_type = ParseMetricType(metric_declaration.metric_type);
  switch (metric_type) {
    case ASTL_METRIC_VALUE:
    case ASTL_METRIC_EVENT:
    case ASTL_METRIC_DELTA:
    case ASTL_METRIC_RATE:
      return CreateBasicMetricConfigs(metric_key_name, metric_declaration, scmi_spec, metric_type, applicable_targets);
    case ASTL_METRIC_FINITE_SET_VALUE:
      return CreateFiniteSetMetricConfigs(metric_key_name, metric_declaration, scmi_spec, applicable_targets);
    case ASTL_METRIC_RESIDENCY:
      return CreateResidencyMetricConfigs(metric_key_name, metric_declaration, scmi_spec, applicable_targets);
    default:
      ASTL_LOG_ERROR("Unsupported or unknown metric type '{}' for metric {}", metric_declaration.metric_type,
                     metric_key_name);
      return std::unexpected(astl::kInternalNotImplemented);
  }
}

}  // namespace astl::metrics::spec
