// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/metric_json_declaration.hpp"

#include <cmath>
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

auto ParseValueType(const MetricJsonDeclaration& metric_declaration) -> astl_value_type_t {
  // alternatively parse the size field of the scmi spec
  (void)metric_declaration;  // unused in this implementation
  return ASTL_VALUE_UINT64;
}

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

  for (const auto& scmi_metric_declaration : metric_registers) {
    const auto&      metric_name          = scmi_metric_declaration.GetFullyQualifiedName();
    const auto       units                = scmi_metric_declaration.units;
    const auto       base10_unit_modifier = scmi_metric_declaration.base10_unit_modifier;
    constexpr double base10               = 10.0;
    const double     value_scale_factor   = std::pow(base10, static_cast<double>(base10_unit_modifier));

    // if there's a base10 multiplier, need to  treat it as a float, otherwise use configured type.
    const auto  value_type = base10_unit_modifier ? ASTL_VALUE_FLOAT64 : ParseValueType(metric_declaration);
    const auto& de_id      = scmi_metric_declaration.de_id;
    // @todo(ASTL-186) - may need to handle different data event ids for different targets
    ScmiOperationBuilder operation_builder{de_id, value_scale_factor};
    auto                 finite_set_copy = finite_set;     // copy for this metric instance
    auto                 info_copy       = value_to_info;  // copy for this metric instance

    auto formula_result = BuildFormula(metric_declaration.formula);
    if (!formula_result.has_value()) {
      return std::unexpected(formula_result.error());
    }

    auto new_metric_config = std::make_unique<FiniteSetMetricConfig>(
        // @todo(ASTL-303) - consider how best to use the base10_unit_modifier in the MetricConfig and collector
        metric_name, metric_declaration.description, units, value_type, ASTL_METRIC_FINITE_SET_VALUE, category,
        collector_type.value(), std::move(operation_builder), std::move(finite_set_copy), std::move(info_copy),
        std::move(formula_result.value()));

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

    auto state_iter = matching_scmi_register_definitions.state_to_base_data_event_id.find(state_name);
    if (state_iter == matching_scmi_register_definitions.state_to_base_data_event_id.end()) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} not found in matching SCMI registers", state_name,
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    auto data_event_id = scmi::spec::GetDataEventId(state_iter->second, instance);
    // @todo(ASTL-331) support non-zero base10 unit modifiers for residency metrics.
    ScmiOperationBuilder             operation_builder{data_event_id};
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
  // @todo(ASTL-331) if supporting base10 unit modifiers for residency metrics, need to make the value type f64
  const auto value_type     = ParseValueType(metric_declaration);
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
        metric_declaration.inferred_state, std::move(formula_result.value()));

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

  for (const auto& scmi_metric_declaration : metric_registers) {
    const auto           units                = scmi_metric_declaration.units;
    const auto           base10_unit_modifier = scmi_metric_declaration.base10_unit_modifier;
    constexpr double     base10               = 10.0;
    const double         value_scale_factor   = std::pow(base10, static_cast<double>(base10_unit_modifier));
    ScmiOperationBuilder operation_builder{scmi_metric_declaration.de_id, value_scale_factor};
    // if there's a base10 unit modifier, we need to treat the value as a float and apply the modifier in the collector
    const auto value_type = base10_unit_modifier ? ASTL_VALUE_FLOAT64 : ParseValueType(metric_declaration);

    auto formula_result = BuildFormula(metric_declaration.formula);
    if (!formula_result.has_value()) {
      return std::unexpected(formula_result.error());
    }

    auto metric_groups     = metric_declaration.metric_groups.value_or(std::vector<std::string>{});
    auto new_metric_config = std::make_unique<MetricConfig>(
        scmi_metric_declaration.GetFullyQualifiedName(), metric_declaration.description, units, value_type, category,
        metric_type, std::move(metric_groups), collector_type.value(), std::move(operation_builder),
        std::move(formula_result.value()));
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
