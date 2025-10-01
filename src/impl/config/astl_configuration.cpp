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

#include "config/astl_configuration.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"

using json = nlohmann::json;

namespace astl {

inline void from_json(const json& json_data, MetricJsonDeclaration& metric) {
  json_data.at("description").get_to(metric.description);
  json_data.at("unit").get_to(metric.unit);
  json_data.at("metric_type").get_to(metric.metric_type);
  json_data.at("collection_protocol").get_to(metric.collection_protocol);

  // Register field is optional for residency metrics (they have individual state registers)
  if (json_data.contains("register")) {
    json_data.at("register").get_to(metric.register_name);
  }

  // Handle residency-specific fields
  if (json_data.contains("inferred_state")) {
    metric.inferred_state = json_data["inferred_state"].get<std::string>();
  }
  if (json_data.contains("states")) {
    metric.states = json_data["states"].get<std::map<std::string, nlohmann::json>>();
  }

  // Handle finite set specific fields
  if (json_data.contains("finite_set_values")) {
    metric.finite_set_values = json_data["finite_set_values"].get<std::vector<nlohmann::json>>();
  }
}

inline void from_json(const json& json_data, AstlConfiguration& cfg) {
  // optional string: j.value(key, default_opt) works nicely
  //  cfg.scmi_sysfs_telemetry_root_path =
  if (const auto path = json_data.value("scmi_sysfs_telemetry_root_path", ""); !path.empty()) {
    cfg.scmi_sysfs_telemetry_root_path = path;
  }

  // required vector<MetricJsonDeclaration> — will throw if missing
  json_data.at("metrics").get_to(cfg.metric_declarations);

  // another optional string
  if (const auto path = json_data.value("scmi_specification_path", ""); !path.empty()) {
    cfg.scmi_specification_path = path;
  }
}

auto ParseConfiguration(std::string_view configuration_data) -> std::expected<AstlConfiguration, astl_status_code> {
  try {
    json json_data = json::parse(configuration_data);
    return json_data.get<AstlConfiguration>();
  } catch (nlohmann::json::parse_error const& e) {
    ASTL_LOG_ERROR("Parse error reading ASTL config file: {}", e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

auto ParseConfiguration(std::istream& configuration_data) -> std::expected<AstlConfiguration, astl_status_code> {
  if (!configuration_data) {
    ASTL_LOG_ERROR("Null configuration data given to GetConfiguration");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  // Read the entire content of the istream into a string buffer
  std::string file_content((std::istreambuf_iterator<char>(configuration_data)), std::istreambuf_iterator<char>());
  return ParseConfiguration(std::string_view(file_content));
}

auto ParseUnits(const MetricJsonDeclaration& metric_declaration) -> astl_units_t {
  auto unit_str = astl::ToLowerCopy(metric_declaration.unit);
  if (unit_str == "none" || unit_str.empty()) {
    return ASTL_UNITS_NONE;
  }
  if (unit_str == "ticks") {
    return ASTL_UNITS_TICKS;
  }
  if (unit_str == "s" || unit_str == "sec" || unit_str == "second" || unit_str == "seconds") {
    return ASTL_UNITS_SECONDS;
  }
  if (unit_str == "c" || unit_str == "celcius") {
    return ASTL_UNITS_CELSIUS;
  }
  if (unit_str == "j" || unit_str == "joule" || unit_str == "joules") {
    return ASTL_UNITS_JOULES;
  }
  if (unit_str == "w" || unit_str == "watt" || unit_str == "watts") {
    return ASTL_UNITS_WATTS;
  }
  if (unit_str == "v" || unit_str == "volt" || unit_str == "volts") {
    return ASTL_UNITS_VOLTS;
  }
  if (unit_str == "a" || unit_str == "amp" || unit_str == "amps") {
    return ASTL_UNITS_AMPS;
  }
  if (unit_str == "b" || unit_str == "byte" || unit_str == "bytes") {
    return ASTL_UNITS_BYTES;
  }
  if (unit_str == "mbps" || unit_str == "mb/s") {
    return ASTL_UNITS_MBYTESPERSEC;
  }
  if (unit_str == "mhz") {
    return ASTL_UNITS_MHERTZ;
  }
  return ASTL_UNITS_UNKNOWN;
}

auto ParseValueType(const MetricJsonDeclaration& metric_declaration) -> astl_value_type_t {
  // alternatively parse the size field of the scmi spec
  (void)metric_declaration;  // unused in this implementation
  return ASTL_VALUE_UINT64;
}

auto ParseMetricType(const MetricJsonDeclaration& metric_declaration) -> astl_metric_type_t {
  auto metric_type_lower = astl::ToLowerCopy(metric_declaration.metric_type);
  if (metric_type_lower == "val" || metric_type_lower == "value") {
    return ASTL_METRIC_VALUE;
  }
  if (metric_type_lower == "set" || metric_type_lower == "finite" || metric_type_lower == "finite_set") {
    return ASTL_METRIC_FINITE_SET_VALUE;
  }
  if (metric_type_lower == "e" || metric_type_lower == "event") {
    return ASTL_METRIC_EVENT;
  }
  if (metric_type_lower == "d" || metric_type_lower == "delta") {
    return ASTL_METRIC_DELTA;
  }
  if (metric_type_lower == "residency") {
    return ASTL_METRIC_RESIDENCY;
  }
  if (metric_type_lower == "r" || metric_type_lower == "rate") {
    return ASTL_METRIC_RATE;
  }
  return ASTL_METRIC_UNKNOWN;
}

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType> {
  auto collector_type_lower = astl::ToLowerCopy(metric_declaration.collection_protocol);
  if (collector_type_lower == "scmi") {
    return CollectorType::SCMI;
  }
  return std::nullopt;
}

/**
 * @brief Given a residency metric name, scan the metric declarations (from config file) and scmi_spec
 * defining the platform to create a map of layout member (e.g. AP0) to collection of StateInfo (holding state names,
 * data event ids)
 *
 * @param metric_key_name    The string value used as an index into the contents of a layout member (e.g.
 * 'ENERGY_COUNTER' within 'AP0')
 * @param metric_declaration The data from the json configuration file specifying this metric's states
 * @param scmi_spec          The scmi::ScmiSpecification including the relevant 'layout' section of the SCMI spec json
 * for this platform
 *
 * @returns a map of layout member name to StateToInfoMap representing the states of this metric.
 */
auto GetResidencyMetricStateToInfoMap(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                                      scmi::ScmiSpecification const& scmi_spec)
    -> std::expected<std::unordered_map<std::string, ResidencyMetricConfig::StateToInfoMap>, astl_status_code> {
  // Check if states are defined
  if (!metric_declaration.states.has_value()) {
    ASTL_LOG_ERROR("Residency metric {} missing 'states' configuration", metric_key_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection_protocol,
                   metric_key_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  std::unordered_map<std::string, ResidencyMetricConfig::StateToInfoMap> per_member_state_infos;

  // for each configured state in the defined metric, identify its register (scmi data event)
  for (const auto& [state_name, state_config] : metric_declaration.states.value()) {
    // Extract register name from state configuration
    if (!state_config.contains("register")) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} missing 'register' field", state_name, metric_key_name);
      continue;
    }
    std::string register_name = state_config["register"].get<std::string>();

    // Extract tick frequency from state configuration (required field)
    // tick_frequency is in Hz and represents the frequency at which state residency counters are updated.
    // This is used to convert raw counter values to time units (e.g., ticks to seconds).
    if (!state_config.contains("tick_frequency")) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} missing required 'tick_frequency' field", state_name,
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    double tick_frequency = state_config["tick_frequency"].get<double>();  // Frequency in Hz

    // each layout member is a pair of string names (e.g. 'AP0') and a map of string names to register info
    for (const auto& [member_name, registers] : scmi_spec.layout.members) {
      auto data_event_id = scmi::GetDataEventIdForLayoutMember(register_name, member_name, registers);
      if (!data_event_id) {
        ASTL_LOG_ERROR("No Data Event IDs found for state '{}' register '{}' in metric {}", state_name, register_name,
                       metric_key_name);
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      // @todo(ASTL-186) consider ScmiMultiTargetOperationBuilder
      //                 if we want to support different data event ids per target
      ScmiOperationBuilder             operation_builder{data_event_id.value()};
      ResidencyMetricConfig::StateInfo state_info{state_name, tick_frequency, std::move(operation_builder)};
      per_member_state_infos[member_name][state_info.state_name] = std::move(state_info);
    }
  }
  return per_member_state_infos;
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
  if (val.is_string()) {
    return AstlValue{val.get<std::string>()};
  }

  ASTL_LOG_ERROR("Unsupported JSON value type for label '{}' in finite_set_values (metric {})", label, metric_key_name);
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

auto CreateFiniteSetMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                                  scmi::ScmiSpecification const&     scmi_spec,
                                  std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  // scmi_targets unused in this implementation - may need to change if different targets have different data event ids
  // for the same metric
  (void)scmi_targets;
  if (!metric_declaration.finite_set_values.has_value()) {
    ASTL_LOG_ERROR("Finite set metric {} missing 'finite_set_values' configuration", metric_key_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for finite set metric {}", metric_declaration.collection_protocol,
                   metric_key_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  auto metric_registers = scmi::GetMetricRegisters(metric_declaration.register_name, scmi_spec.layout);
  if (metric_registers.empty()) {
    ASTL_LOG_ERROR("No Data Event IDs found for finite set metric {} (register '{}')", metric_key_name,
                   metric_declaration.register_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  // Expect: finite_set_values = [ {"P0":0}, {"P1":1}, ... ] each element exactly one key -> primitive value
  FiniteSetMetricConfig::FiniteSet           finite_set;      // unique values
  std::unordered_map<std::string, AstlValue> label_to_value;  // label to AstlValue mapping (temporary use for parsing)

  for (const auto& obj : metric_declaration.finite_set_values.value()) {
    if (!obj.is_object() || obj.size() != 1) {
      ASTL_LOG_ERROR("Each element of finite_set_values for metric {} must be an object with exactly one key",
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    const auto  it    = obj.begin();
    const auto& label = it.key();
    const auto& val   = it.value();
    if (label.empty()) {
      ASTL_LOG_ERROR("Empty label in finite_set_values for metric {}", metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    auto parsed_value_result = ParseJsonValueToAstlValue(val, label, metric_key_name);
    if (!parsed_value_result) {
      return std::unexpected(parsed_value_result.error());
    }

    if (!label_to_value.emplace(label, *parsed_value_result).second) {
      ASTL_LOG_ERROR("Duplicate finite set label '{}' in metric {}", label, metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    finite_set.emplace(*parsed_value_result);
  }

  if (finite_set.empty()) {
    ASTL_LOG_ERROR("Empty finite set for metric {}", metric_key_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ASTL_LOG_INFO("Parsed finite set metric '{}' with {} labels and {} unique values", metric_key_name,
                label_to_value.size(), finite_set.size());

  // Convert label_to_value map to value_to_label map for use in FiniteSetMetricConfig
  FiniteSetMetricConfig::ValueToLabelMap value_to_label;
  for (const auto& [label, value] : label_to_value) {
    value_to_label[value] = label;
  }

  std::vector<std::unique_ptr<MetricConfig>> metric_configs;
  metric_configs.reserve(metric_registers.size());
  const auto units      = ParseUnits(metric_declaration);
  const auto value_type = ParseValueType(metric_declaration);
  for (const auto& name_and_de_id : metric_registers) {
    const auto& metric_name = name_and_de_id.first;
    const auto& de_id       = name_and_de_id.second;
    // @todo(ASTL-186) - may need to handle different data event ids for different targets
    ScmiOperationBuilder operation_builder{de_id};
    auto                 finite_set_copy = finite_set;      // copy for this metric instance
    auto                 labels_copy     = value_to_label;  // copy for this metric instance
    metric_configs.push_back(std::make_unique<FiniteSetMetricConfig>(
        metric_name, metric_declaration.description, units, value_type, ASTL_METRIC_FINITE_SET_VALUE,
        collector_type.value(), std::move(operation_builder), std::move(finite_set_copy), std::move(labels_copy)));
  }
  ASTL_LOG_INFO("Created {} finite set metric config(s) for '{}' with {} valid values", metric_configs.size(),
                metric_key_name, finite_set.size());
  return metric_configs;
}

/**
 * @brief Create a ResidencyMetricConfig from a MetricJsonDeclaration for each member in the layout that has a matching
 * metric_key_name
 * @param scmi_spec          The scmi::ScmiSpecification including the relevant 'layout' section of the SCMI spec json
 * for this platform
 */
auto CreateResidencyMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                                  scmi::ScmiSpecification const&     scmi_spec,
                                  std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  auto per_member_state_info = GetResidencyMetricStateToInfoMap(metric_key_name, metric_declaration, scmi_spec);
  if (!per_member_state_info) {
    return std::unexpected(per_member_state_info.error());
  }

  const auto units          = ParseUnits(metric_declaration);
  const auto value_type     = ParseValueType(metric_declaration);
  const auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection_protocol,
                   metric_key_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }

  std::vector<std::unique_ptr<MetricConfig>> metric_configs;
  for (const auto& [layout_member_name, state_info_map] : per_member_state_info.value()) {
    ResidencyMetricConfig::TargetToStateToInfoMap per_target_state_info;

    for (const auto* target : scmi_targets) {
      if (target->GetCollectorType() != CollectorType::SCMI) {
        continue;
      }
      per_target_state_info[target->Name()] = state_info_map;
    }

    // assemble a name for this metric config - use the layout member name (e.g. 'AP0' with an '_' as a prefix to more
    // uniquely identify it)
    std::string metric_name{layout_member_name + "_" + std::string(metric_key_name)};
    metric_configs.push_back(std::make_unique<ResidencyMetricConfig>(
        std::move(metric_name), metric_declaration.description, units, value_type, ASTL_METRIC_RESIDENCY,
        collector_type.value(), std::move(per_target_state_info), metric_declaration.inferred_state));
  }
  return metric_configs;
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
                              scmi::ScmiSpecification const& scmi_spec, astl_metric_type_t metric_type,
                              std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type || collector_type != CollectorType::SCMI) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection_protocol,
                   metric_key_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  // For non-residency metrics, use the existing logic
  auto metric_registers = scmi::GetMetricRegisters(metric_declaration.register_name, scmi_spec.layout);
  if (metric_registers.empty()) {
    ASTL_LOG_INFO("No Data Event IDs found for metric {}", metric_key_name);
  }
  std::vector<std::unique_ptr<MetricConfig>> metric_configs;
  metric_configs.reserve(metric_registers.size());
  const auto units                = ParseUnits(metric_declaration);
  const auto value_type           = ParseValueType(metric_declaration);
  auto       create_metric_config = [&](const auto& metric_name_and_de_id) {
    const auto& [metric_name, de_id] = metric_name_and_de_id;

    // @todo(ASTL-186) consider ScmiMultiTargetOperationBuilder
    //                 if we want to support different data event ids per target
    (void)scmi_targets;
    ScmiOperationBuilder operation_builder{de_id};
    return std::make_unique<MetricConfig>(metric_name, metric_declaration.description, units, value_type, metric_type,
                                                collector_type.value(), std::move(operation_builder));
  };

  std::transform(std::begin(metric_registers), std::end(metric_registers), std::back_inserter(metric_configs),
                 create_metric_config);
  return metric_configs;
}

/**
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_key_name    The string key from scmi specification json in the layout.members.<member>. entries list,
 * e.g. 'ENERGY_COUNTER'
 * @param metric_declaration The MetricJsonDeclaration json definition of this type of metric from the astl
 * configuration json. adds info like units on how to interpret the metrics
 * @param scmi_spec          The scmi::ScmiSpecification for this platform
 * @param scmi_targets       The detected scmi telemetry endpoints on this platform
 */
auto CreateScmiMetricConfigs(std::string_view metric_key_name, MetricJsonDeclaration const& metric_declaration,
                             scmi::ScmiSpecification const& scmi_spec, std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  auto metric_type = ParseMetricType(metric_declaration);
  switch (metric_type) {
    case ASTL_METRIC_VALUE:
    case ASTL_METRIC_EVENT:
    case ASTL_METRIC_DELTA:
    case ASTL_METRIC_RATE:
      return CreateBasicMetricConfigs(metric_key_name, metric_declaration, scmi_spec, metric_type, scmi_targets);
    case ASTL_METRIC_FINITE_SET_VALUE:
      return CreateFiniteSetMetricConfigs(metric_key_name, metric_declaration, scmi_spec, scmi_targets);
    case ASTL_METRIC_RESIDENCY:
      return CreateResidencyMetricConfigs(metric_key_name, metric_declaration, scmi_spec, scmi_targets);
    default:
      ASTL_LOG_ERROR("Unsupported or unknown metric type '{}' for metric {}", metric_declaration.metric_type,
                     metric_key_name);
      return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

}  // namespace astl
