// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/metric_json_declaration.hpp"

#include <cstdint>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"
#include "metric/formula_builder.hpp"

using json = nlohmann::json;

namespace astl::metrics::spec {

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType> {
  auto collector_type_lower = astl::ToLowerCopy(metric_declaration.collection.protocol);
  if (collector_type_lower == "scmi") {
    return CollectorType::SCMI;
  }
  if (collector_type_lower == "libsensors") {
    return CollectorType::LIBSENSORS;
  }
  return std::nullopt;
}

auto ParseValueType(const MetricJsonDeclaration& metric_declaration) -> astl_value_type_t {
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type.has_value()) {
    return ASTL_VALUE_UNKNOWN;
  }
  switch (*collector_type) {
    case CollectorType::SCMI:
      // SCMI collector reads deXX files as unsigned 64-bit raw values by contract.
      // Keep input typing fixed here unless collector semantics change.
      return ASTL_VALUE_UINT64;
    case CollectorType::LIBSENSORS:
      // Libsensors readings are normalized as doubles by the collector implementation.
      return ASTL_VALUE_FLOAT64;
    default:
      return ASTL_VALUE_UNKNOWN;
  }
}

auto ParseScmiOutputValueType(astl_value_type_t input_value_type, int32_t base10_unit_modifier) -> astl_value_type_t {
  if (input_value_type == ASTL_VALUE_UNKNOWN) {
    return ASTL_VALUE_UNKNOWN;
  }
  // SCMI base10 scaling is implemented through ScalingFormula, which always yields float64.
  if (base10_unit_modifier != 0) {
    return ASTL_VALUE_FLOAT64;
  }
  return input_value_type;
}

auto BuildScalingFormulaFromBase10Modifier(int32_t base10_unit_modifier) -> AnyFormula {
  // Convert protocol metadata (base10 exponent) into a protocol-agnostic formula step.
  if (base10_unit_modifier == 0) {
    return AnyFormula{IdentityFormula{}};
  }
  constexpr int      chunk_exponent = 19;  // 10^19 fits in uint64.
  constexpr uint64_t chunk_literal  = 10000000000000000000ULL;
  constexpr uint64_t decimal_radix  = 10ULL;

  auto append_scale_step = [](AnyFormula formula, uint64_t numerator, uint64_t denominator) -> AnyFormula {
    // Compose as explicit pipeline stages so very large powers of ten stay representable.
    return ComposeFormulas(std::move(formula), AnyFormula{
                                                   ScalingFormula{numerator, denominator}
    });
  };

  AnyFormula result = AnyFormula{IdentityFormula{}};
  int64_t    exponent{base10_unit_modifier};
  const bool is_positive = exponent > 0;
  if (!is_positive) {
    exponent = -exponent;  // safe for INT32_MIN via int64_t widening above
  }
  // Guard against pathological modifiers that would otherwise build massive pipelines.
  // Note: we intentionally do not collapse large negative exponents to zero because scaling
  // is applied in float64 space and callers can rely on non-zero fractional results.
  constexpr int64_t max_supported_exponent = 190;  // 10 chunk-steps of 10^19.
  if (exponent > max_supported_exponent) {
    ASTL_LOG_WARNING("Clamping base10_unit_modifier {} to {} for scaling pipeline construction", base10_unit_modifier,
                     is_positive ? max_supported_exponent : -max_supported_exponent);
    exponent = max_supported_exponent;
  }

  while (exponent >= chunk_exponent) {
    // Chunk by 10^19 to stay within uint64 literal range and keep formulas parser-friendly.
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

/**
 * @brief Parse a JSON value into an AstlValue based on its type.
 *
 * @param val The JSON value to parse
 * @param label The label associated with the value (for error reporting)
 * @param metric_key_name The metric name (for error reporting)
 * @return std::expected<AstlValue, astl_status_code> The parsed AstlValue or error status
 */
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
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  auto metric_registers = scmi::spec::GetMetricRegistersScmiData(metric_declaration, scmi_spec);
  if (metric_registers.empty()) {
    ASTL_LOG_ERROR("No Data Event IDs found for finite set metric {} (register '{}')", metric_key_name,
                   metric_declaration.collection.register_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  // Expect: finite_set_values = {"P0": {"value": 0, "description": "..."}, "P1": {"value": 1, "description": "..."},
  // ...}
  FiniteSetMetricConfig::FiniteSet      finite_set;  // unique values
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
  const auto            category = ParseCategory(metric_declaration.category);
  // SCMI collection remains uint64 on-wire; output value type may change after scaling.
  const auto input_value_type = ParseValueType(metric_declaration);
  for (const auto& scmi_metric_declaration : metric_registers) {
    const auto&             metric_name          = scmi_metric_declaration.GetFullyQualifiedName();
    const auto              units                = scmi_metric_declaration.units;
    const int32_t           base10_unit_modifier = scmi_metric_declaration.base10_unit_modifier;
    const astl_value_type_t value_type           = ParseScmiOutputValueType(input_value_type, base10_unit_modifier);
    const auto&             de_id                = scmi_metric_declaration.de_id;
    // @todo(ASTL-186) - may need to handle different data event ids for different targets
    ScmiOperationBuilder operation_builder{de_id};
    auto                 finite_set_copy = finite_set;     // copy for this metric instance
    auto                 info_copy       = value_to_info;  // copy for this metric instance

    auto formula_result = BuildFormula(metric_declaration.formula);
    if (!formula_result.has_value()) {
      return std::unexpected(formula_result.error());
    }
    // Apply user formula first, then protocol-derived scaling uniformly in the formula pipeline.
    auto composed_formula =
        ComposeFormulas(std::move(formula_result.value()), BuildScalingFormulaFromBase10Modifier(base10_unit_modifier));

    auto new_metric_config = std::make_unique<FiniteSetMetricConfig>(
        metric_name, metric_declaration.description, units, value_type, ASTL_METRIC_FINITE_SET_VALUE, category,
        collector_type.value(), std::move(operation_builder), std::move(finite_set_copy), std::move(info_copy),
        std::move(composed_formula), input_value_type);

    metric_configs_on_targets.emplace(std::move(new_metric_config), applicable_targets);
  }
  ASTL_LOG_INFO("Created {} finite set metric config(s) for '{}' with {} valid values",
                metric_configs_on_targets.size(), metric_key_name, finite_set.size());
  return metric_configs_on_targets;
}

/**
 * @brief Helper to convert a residency metric declaration and matching json scmi register specifications
 * into a StateToInfoMap for a specific instance number to build a ResidencyMetricConfig object.
 */
static auto GetResidencyMetricStateToInfoMapForInstance(
    std::string_view                                     metric_key_name,
    scmi::spec::ResidencyStateRegisterDefinitions const& matching_scmi_register_definitions,
    scmi::spec::InstanceId instance, MetricJsonDeclaration const& metric_declaration)
    -> std::expected<ResidencyMetricConfig::StateToInfoMap, astl_status_code> {
  ResidencyMetricConfig::StateToInfoMap state_to_info_map;

  for (const auto& [state_name, state_config] : metric_declaration.states.value()) {
    // Extract tick frequency from state configuration (required field)
    // tick_frequency is in Hz and represents the frequency at which state residency counters are updated.
    // This is used to convert raw counter values to time units (e.g., ticks to seconds).
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
    double      tick_frequency    = state_config["tick_frequency"].get<double>();  // Frequency in Hz
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

/**
 * @brief Create a ResidencyMetricConfig from a MetricJsonDeclaration for each member in the layout that has a matching
 * metric_key_name
 * @param scmi_spec          The scmi::ScmiSpecification including the relevant 'layout' section of the SCMI spec json
 * for this platform
 */
auto CreateResidencyMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                                  scmi::spec::ScmiSpecification const& scmi_spec,
                                  std::vector<const ITarget*> const&   applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  MetricConfigOnTargets metric_configs_on_targets;

  // get some essential data from the metric_declaration
  const auto category       = ParseCategory(metric_declaration.category);
  const auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection.protocol,
                   metric_key_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  auto metric_groups = metric_declaration.metric_groups.value_or(std::vector<std::string>{});
  // look through the scmi spec to find matching registers
  auto matching_scmi_register_definitions = FindMatchingScmiRegistersForResidency(metric_declaration, scmi_spec);
  if (!matching_scmi_register_definitions) {
    return std::unexpected(matching_scmi_register_definitions.error());
  }
  // Residency state counters come from SCMI raw reads, so input type remains uint64.
  const auto value_type       = ParseValueType(metric_declaration);
  const auto input_value_type = value_type;

  for (scmi::spec::InstanceId instance = 0; instance < matching_scmi_register_definitions->count; ++instance) {
    // if an instance number is specified, only create one metric config for that instance
    if (metric_declaration.collection.scmi_instance_filter.has_value() &&
        std::to_string(instance) != metric_declaration.collection.scmi_instance_filter.value()) {
      continue;
    }
    std::string name = matching_scmi_register_definitions->count > 1 ? std::format("{}.{}", metric_key_name, instance)
                                                                     : std::string{metric_key_name};
    auto        state_to_info_map_result = GetResidencyMetricStateToInfoMapForInstance(
        metric_key_name, *matching_scmi_register_definitions, instance, metric_declaration);
    if (!state_to_info_map_result.has_value()) {
      return std::unexpected(state_to_info_map_result.error());
    }

    auto formula_result = BuildFormula(metric_declaration.formula);
    if (!formula_result.has_value()) {
      return std::unexpected(formula_result.error());
    }

    auto new_config = std::make_unique<ResidencyMetricConfig>(
        name, metric_declaration.description, ParseUnits(metric_declaration.unit.value_or("")), value_type,
        ASTL_METRIC_RESIDENCY, category, collector_type.value(), std::move(state_to_info_map_result.value()),
        metric_declaration.inferred_state, std::move(formula_result.value()), input_value_type);

    metric_configs_on_targets.emplace(std::move(new_config), applicable_targets);
  }
  return metric_configs_on_targets;
}

/**
 * @brief Create a collection of basic MetricConfig instances from a MetricJsonDeclarations, each of which matches the
 * 'metric_key_name'. Could return up to N metrics, where N is the number of members in the given `layout`. (Say the
 * members are AP0-7, we might get 8 metrics back with names like AP[0-7]_ENERGY_COUNTER)
 * @param metric_key_name    The string key from scmi specification json in the layout.members.<member>. entries list,
 * e.g. 'ENERGY_COUNTER'
 * @param metric_declaration The MetricJsonDeclaration json definition of this type of metric from the astl
 * configuration json. adds info like units on how to interpret the metrics
 * @param scmi_spec          The scmi::ScmiSpecification including the relevant 'layout' section of the SCMI spec json
 * for this platform
 * @param metric_type        The astl_metric_type_t enum specifying this metric type (e.g. SampledValue, Delta, etc)
 */
auto CreateBasicMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                              scmi::spec::ScmiSpecification const& scmi_spec, astl_metric_type_t metric_type,
                              std::vector<const ITarget*> const& applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code> {
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection.protocol,
                   metric_key_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  // For non-residency metrics, use the existing logic
  auto metric_registers = scmi::spec::GetMetricRegistersScmiData(metric_declaration, scmi_spec);
  if (metric_registers.empty()) {
    ASTL_LOG_INFO("No Data Event IDs found for metric {}", metric_key_name);
  }
  MetricConfigOnTargets metric_configs_on_targets;
  const auto            category = ParseCategory(metric_declaration.category);
  // SCMI collection remains uint64 on-wire; output value type may change after scaling.
  const auto input_value_type = ParseValueType(metric_declaration);

  for (const auto& scmi_metric_declaration : metric_registers) {
    const auto              units                = scmi_metric_declaration.units;
    const int32_t           base10_unit_modifier = scmi_metric_declaration.base10_unit_modifier;
    ScmiOperationBuilder    operation_builder{scmi_metric_declaration.de_id};
    const astl_value_type_t value_type = ParseScmiOutputValueType(input_value_type, base10_unit_modifier);

    auto formula_result = BuildFormula(metric_declaration.formula);
    if (!formula_result.has_value()) {
      return std::unexpected(formula_result.error());
    }
    // Apply user formula first, then protocol-derived scaling uniformly in the formula pipeline.
    auto composed_formula =
        ComposeFormulas(std::move(formula_result.value()), BuildScalingFormulaFromBase10Modifier(base10_unit_modifier));

    auto metric_groups     = metric_declaration.metric_groups.value_or(std::vector<std::string>{});
    auto new_metric_config = std::make_unique<MetricConfig>(
        scmi_metric_declaration.GetFullyQualifiedName(), metric_declaration.description, units, value_type, category,
        metric_type, std::move(metric_groups), collector_type.value(), std::move(operation_builder),
        std::move(composed_formula), input_value_type);
    metric_configs_on_targets.emplace(std::move(new_metric_config), applicable_targets);
  }
  return metric_configs_on_targets;
}

/**
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_key_name    The string key from scmi specification json in the layout.members.<member>. entries list,
 * e.g. 'ENERGY_COUNTER'
 * @param metric_declaration The MetricJsonDeclaration json definition of this type of metric from the astl
 * configuration json. adds info like units on how to interpret the metrics
 * @param scmi_spec          The scmi::ScmiSpecification for this platform
 * @param applicable_targets The detected scmi telemetry endpoints on this platform that can be used to collect this
 * metric
 */
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
      return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

}  // namespace astl::metrics::spec
