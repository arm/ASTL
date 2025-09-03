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
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "common/capabilities.hpp"
#include "metric/metric_config.hpp"

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

auto ParseConfiguration(std::istream& configuration_data) -> std::expected<AstlConfiguration, astl_status_code> {
  if (!configuration_data) {
    ASTL_LOG_ERROR("Null configuration data given to GetConfiguration");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  try {
    json json_data = json::parse(configuration_data);
    return json_data.get<AstlConfiguration>();
  } catch (nlohmann::json::parse_error const& e) {
    ASTL_LOG_ERROR("Parse error reading ASTL config file: {}", e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

// @todo (https://jira.arm.com/browse/ASTL-169) Adopt a unit conversion library to provide type-safe unit handling,
// automatic conversions, and better compile-time unit validation instead of the current string-based parsing approach.
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
 * @brief Create a ResidencyMetricConfig from a MetricJsonDeclaration
 */
auto CreateResidencyMetricConfig(std::string_view metric_name, MetricJsonDeclaration const& metric_declaration,
                                 scmi::Layout const& layout)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  // Check if states are defined
  if (!metric_declaration.states.has_value()) {
    ASTL_LOG_ERROR("Residency metric {} missing 'states' configuration", metric_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ResidencyMetricConfig::ScmiTargetToStateToInfoMap state_info;

  // Process each state definition
  for (const auto& [state_name, state_config] : metric_declaration.states.value()) {
    // Extract register name from state configuration
    if (!state_config.contains("register")) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} missing 'register' field", state_name, metric_name);
      continue;
    }

    std::string register_name = state_config["register"].get<std::string>();

    // Extract tick frequency from state configuration (required field)
    // tick_frequency is in Hz and represents the frequency at which state residency counters are updated.
    // This is used to convert raw counter values to time units (e.g., ticks to seconds).
    if (!state_config.contains("tick_frequency")) {
      ASTL_LOG_ERROR("State '{}' in residency metric {} missing required 'tick_frequency' field", state_name,
                     metric_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    double tick_frequency = state_config["tick_frequency"].get<double>();  // Frequency in Hz

    // Find data event IDs for this register across all targets
    auto register_data_event_ids = scmi::GetDataEventIdsForMetric(register_name, layout);
    if (register_data_event_ids.empty()) {
      ASTL_LOG_ERROR("No Data Event IDs found for state '{}' register '{}' in metric {}", state_name, register_name,
                     metric_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    // Add the data event IDs and tick frequency for this state to each target
    for (const auto& [target_name, data_event_ids] : register_data_event_ids) {
      // Note: For residency metrics, there is only one event ID per state, but data_event_ids is a vector
      // as it's a generic data structure designed to support all metric types.

      // Check for empty data_event_ids
      if (data_event_ids.empty()) {
        ASTL_LOG_ERROR("No data event IDs found for state '{}' register '{}' on target '{}'", state_name, register_name,
                       target_name);
        continue;
      }

      // Check for multiple data_event_ids and warn
      if (data_event_ids.size() > 1) {
        ASTL_LOG_WARNING(
            "Expected exactly one data event ID for state '{}' register '{}' on target '{}', found {}. Using first "
            "event ID.",
            state_name, register_name, target_name, data_event_ids.size());
      }

      // Use the first data event ID
      ScmiDataEventId data_event_id =
          data_event_ids[0];  // data_event_ids is std::vector<ScmiDataEventId> - generic for all metric types
      state_info[target_name][state_name] = {state_name, data_event_id, tick_frequency};
      ASTL_LOG_DEBUG("Found residency state '{}' for target '{}' with data event ID {} and tick frequency {}Hz",
                     state_name, target_name, data_event_id, tick_frequency);
    }
  }

  if (state_info.empty()) {
    ASTL_LOG_ERROR("No state data event IDs found for residency metric {}", metric_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection_protocol,
                   metric_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }

  return std::make_unique<ResidencyMetricConfig>(std::string(metric_name), metric_declaration.description,
                                                 ParseUnits(metric_declaration), ParseValueType(metric_declaration),
                                                 ASTL_METRIC_RESIDENCY, collector_type.value(), std::move(state_info),
                                                 metric_declaration.inferred_state);
}

/**
 * @brief Create a basic MetricConfig from a MetricJsonDeclaration
 */
auto CreateBasicMetricConfig(std::string_view metric_name, MetricJsonDeclaration const& metric_declaration,
                             scmi::Layout const& layout, astl_metric_type_t metric_type)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  // For non-residency metrics, use the existing logic
  auto data_event_ids = scmi::GetDataEventIdsForMetric(metric_declaration.register_name, layout);
  if (data_event_ids.empty()) {
    ASTL_LOG_ERROR("No Data Event IDs found for metric {}", metric_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection_protocol,
                   metric_name);
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }

  return std::make_unique<MetricConfig>(std::string(metric_name), metric_declaration.description,
                                        ParseUnits(metric_declaration), ParseValueType(metric_declaration), metric_type,
                                        collector_type.value(), std::move(data_event_ids));
}

/**
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_declaration The MetricJsonDeclaration object to convert
 * @param layout The Scmi layout specification containing the Data Event IDs from platform json spec
 */
auto CreateMetricConfig(std::string_view metric_name, MetricJsonDeclaration const& metric_declaration,
                        scmi::Layout const& layout) -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  auto metric_type = ParseMetricType(metric_declaration);

  // Route to appropriate creation function based on metric type
  if (metric_type == ASTL_METRIC_RESIDENCY) {
    return CreateResidencyMetricConfig(metric_name, metric_declaration, layout);
  }

  return CreateBasicMetricConfig(metric_name, metric_declaration, layout, metric_type);
}

}  // namespace astl
