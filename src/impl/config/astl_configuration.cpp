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
  json_data.at("register").get_to(metric.register_name);
  json_data.at("unit").get_to(metric.unit);
  json_data.at("metric_type").get_to(metric.metric_type);
  json_data.at("collection_protocol").get_to(metric.collection_protocol);
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
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_declaration The MetricJsonDeclaration object to convert
 * @param layout The Scmi layout specification containing the Data Event IDs from platform json spec
 */
auto CreateMetricConfig(std::string_view metric_name, MetricJsonDeclaration const& metric_declaration,
                        scmi::Layout const& layout) -> std::unique_ptr<MetricConfig> {
  // find the Data Event ID for this metric
  auto data_event_ids = scmi::GetDataEventIdsForMetric(metric_declaration.register_name, layout);
  if (data_event_ids.empty()) {
    ASTL_LOG_ERROR("No Data Event IDs found for metric {}", metric_name);
    return nullptr;
  }

  // create and return the MetricConfig object
  auto collector_type = ParseCollectorType(metric_declaration);
  if (!collector_type) {
    ASTL_LOG_ERROR("Unsupported collector type '{}' for metric {}", metric_declaration.collection_protocol,
                   metric_name);
    return nullptr;
  }
  return std::make_unique<MetricConfig>(std::string(metric_name), metric_declaration.description,
                                        ParseUnits(metric_declaration), ParseValueType(metric_declaration),
                                        ParseMetricType(metric_declaration), collector_type.value(),
                                        std::move(data_event_ids));
}

}  // namespace astl
