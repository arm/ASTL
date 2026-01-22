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

#ifndef SCMI_PLATFORM_TELEMETRY_SPEC_HPP_
#define SCMI_PLATFORM_TELEMETRY_SPEC_HPP_

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/scmi/uuid.hpp"
#include "operation/scmi_read_operation.hpp"

using json = nlohmann::json;

// forward declaration to avoid circular dependency
namespace astl::metrics::spec {
struct MetricJsonDeclaration;
}

namespace astl::scmi::spec {

using InstanceId = uint16_t;

// for SCMI json spec, the repeated layout members are declared as a base + count,
// and the data event IDs of specific instances are created by adding the index to the upper 16 bits.
inline auto GetDataEventId(ScmiDataEventId base_de_id, InstanceId instance_index) -> ScmiDataEventId {
  constexpr int instance_offset = 16;
  return (static_cast<ScmiDataEventId>(instance_index) << instance_offset) | base_de_id;
}

// parse helping function
inline uint64_t ParseToUint64(const json& json_data, std::string_view field_name) {
  std::string string_for_conversion;
  json_data.at(field_name).get_to(string_for_conversion);
  return std::stoull(string_for_conversion, nullptr, 0);
}

// parse helping function
inline uint32_t ParseToUint32(const json& json_data, std::string_view field_name) {
  std::string string_for_conversion;
  json_data.at(field_name).get_to(string_for_conversion);
  return static_cast<uint32_t>(std::stoul(string_for_conversion, nullptr, 0));
}

inline uint16_t ParseToUint16(const json& json_data, std::string_view field_name) {
  std::string string_for_conversion;
  json_data.at(field_name).get_to(string_for_conversion);
  return static_cast<uint16_t>(std::stoul(string_for_conversion, nullptr, 0));
}

inline int32_t ParseToInt32(const json& json_data, std::string_view field_name) {
  std::string string_for_conversion;
  json_data.at(field_name).get_to(string_for_conversion);
  return static_cast<int32_t>(std::stoi(string_for_conversion, nullptr, 0));
}

////////////////////////////////////////////////////////////////////////////////
//  repometa.json parsing                                                     //
////////////////////////////////////////////////////////////////////////////////

/** @brief A members of the `uuid_mapping` table in repometa.json */
struct TelemetrySpecFile {
  std::string last_updated;
  std::string description;
  std::string specification_file;
};

inline void from_json(const json& json_data, TelemetrySpecFile& spec_file) {
  json_data.at("last_updated").get_to(spec_file.last_updated);
  json_data.at("description").get_to(spec_file.description);
  json_data.at("specification_file").get_to(spec_file.specification_file);
}

/**
 * @brief represents the repometa.json file structure, mainly the uuid mapping to spec files
 */
struct RepoMeta {
  std::string                                             last_updated;
  std::unordered_map<scmi::spec::Uuid, TelemetrySpecFile> uuid_mapping;
};

inline void from_json(const json& json_data, RepoMeta& repo_meta) {
  json_data.at("last_updated").get_to(repo_meta.last_updated);
  for (const auto& [key, value] : json_data.at("uuid_mapping").items()) {
    auto uuid                               = GetNormalizedUuid(key);
    repo_meta.uuid_mapping[std::move(uuid)] = value.get<TelemetrySpecFile>();
  }
}

////////////////////////////////////////////////////////////////////////////////
//  metrics_specification parsing (e.g. scp.json, lcp_cluster.json            //
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief A single member of []layout[n].members[]
 */
struct DataEvent {
  uint32_t    base_de_id{};
  std::string name;
  std::string component;
  std::string description;
  std::string unit;
  int32_t     unit_exponent{};
  uint16_t    rel_offset{};
};

inline void from_json(const json& json_data, DataEvent& entry) {
  entry.base_de_id = ParseToUint32(json_data, "base_de_id");
  json_data.at("name").get_to(entry.name);
  json_data.at("component").get_to(entry.component);
  json_data.at("description").get_to(entry.description);
  json_data.at("unit").get_to(entry.unit);
  if (json_data.contains("unit_exponent")) {
    json_data.at("unit_exponent").get_to(entry.unit_exponent);
  } else {
    entry.unit_exponent = 0;
  }
  entry.rel_offset = ParseToUint16(json_data, "rel_offset");
}

/**
 * @brief A single member of []layout[]
 *
 * @example
 *       {
 *       "count": 72,
 *       "start_offset": 304,
 *       "block_size": 32,
 *       "members": [
 *          {
 *             "base_de_id": "0x0000E56D",
 *             "name": "CXS_READ_BANDWIDTH",
 *             "component": "PSS_BMU",
 *             "description": "Accumulative CXS total read bandwidth",
 *             "unit": "bits",
 *             "unit_exponent": -3,
 *             "rel_offset": "0x0000"
 *          },
 *          {
 *             "base_de_id": "0x0000E56E",
 *             "name": "CXS_WRITE_BANDWIDTH",
 *             "component": "PSS_BMU",
 *             "description": "Accumulative total read bandwidth",
 *             "unit": "bits",
 *             "unit_exponent": -3,
 *             "rel_offset": "0x0010"
 *          }
 *       ]
 *    },
 * represents 144 data events, including
 * - PSS_BMU.0.CXS_READ_BANDWIDTH   at data event 0x0000_E56D
 * - PSS_BMU.0.CXS_WRITE_BANDWIDTH  at data event 0x0000_E56E
 * - PSS_BMU.1.CXS_READ_BANDWIDTH   at data event 0x0001_E56D
 * - PSS_BMU.1.CXS_WRITE_BANDWIDTH  at data event 0x0001_E56E
 * - ...
 * - PSS_BMU.71.CXS_READ_BANDWIDTH  at data event 0x0047_E56D
 * - PSS_BMU.71.CXS_WRITE_BANDWIDTH at data event 0x0047_E56E
 */
struct Layout {
  uint32_t               count{};
  uint64_t               start_offset{};
  uint64_t               block_size{};
  std::vector<DataEvent> members;
};

inline void from_json(const json& json_data, Layout& layout) {
  json_data.at("count").get_to(layout.count);
  json_data.at("start_offset").get_to(layout.start_offset);
  json_data.at("block_size").get_to(layout.block_size);
  json_data.at("members").get_to(layout.members);
}

/** @brief An element of the metrics_specification json files with file metadata */
struct SpecificationDocument {
  std::string timestamp;
  std::string copyright;
  bool        confidential{false};
  std::string quality;
  std::string license;
  std::string description;
};

inline void from_json(const json& json_data, SpecificationDocument& spec_doc) {
  json_data.at("timestamp").get_to(spec_doc.timestamp);
  json_data.at("copyright").get_to(spec_doc.copyright);
  json_data.at("confidential").get_to(spec_doc.confidential);
  json_data.at("quality").get_to(spec_doc.quality);
  json_data.at("license").get_to(spec_doc.license);
  json_data.at("description").get_to(spec_doc.description);
}

struct ScmiSpecification {
  std::string           _type;
  SpecificationDocument specification_document;
  std::string           uuid;
  std::string           description;
  std::string           instance_id;
  std::string           chiplet_id;
  uint32_t              size{};
  std::vector<Layout>   layout;
};

inline void from_json(const json& json_data, ScmiSpecification& spec) {
  json_data.at("_type").get_to(spec._type);
  json_data.at("document").get_to(spec.specification_document);
  json_data.at("uuid").get_to(spec.uuid);
  json_data.at("description").get_to(spec.description);
  json_data.at("instance_id").get_to(spec.instance_id);
  json_data.at("chiplet_id").get_to(spec.chiplet_id);
  json_data.at("size").get_to(spec.size);
  json_data.at("layout").get_to(spec.layout);
}

/**
 * @brief POD class to hold the relevant parts of a metric declaration and SCMI spec entry
 */
struct ScmiMetricDeclaration {
  std::string     name;
  std::string     component;
  std::string     instance;
  astl_units_t    units;
  ScmiDataEventId de_id{};

  ScmiMetricDeclaration(std::string name, std::string component, std::string instance, astl_units_t units,
                        ScmiDataEventId de_id)
      : name(std::move(name)),
        component(std::move(component)),
        instance(std::move(instance)),
        units(units),
        de_id{de_id} {}

  std::string GetFullyQualifiedName() const {
    std::ostringstream oss;
    if (!component.empty()) {
      oss << component << ".";
    }
    if (!instance.empty()) {
      oss << instance << ".";
    }
    oss << name;
    return oss.str();
  }
};

/**
 * @brief Get the collection of fully-qualified SCMI register definitions (i.e. PSS_BMU.0.ENERGY_COUNTER)
 * that match the given metric declaration and their corresponding data event ids.
 * @param register_name The register name to look up (i.e. ENERGY_COUNTER)
 * @param scmi_specification The Scmi specification containing the Data Event IDs
 * @return A collection of ScmiMetricDeclaration entries listing all registers matching generic 'register_name'
 *         and details like the specific component+instance name (e.g. PSS_BMU.1.ENERGY_COUNTER) and DE_ID
 */
auto GetMetricRegistersScmiData(astl::metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                ScmiSpecification const& scmi_specification) -> std::vector<ScmiMetricDeclaration>;

/**
 * @brief A helper to hold SCMI json spec data matching a residency metric's required registers
 */
struct ResidencyStateRegisterDefinitions {
  // number of instances of the members in the matching scmi layout member
  // note that these registers in `state_to_base_data_event_id` might come from different layout members.
  // When matching these registers, it's simply an error if their layout blocks have different 'count' values.
  std::size_t count{0};
  std::string component;  // component name from the scmi spec
  // map of state names (from the residency metric declaration) to base SCMI data event ID.
  std::unordered_map<std::string, ScmiDataEventId> state_to_base_data_event_id;
};

/**
 * @brief Given a residency metric declaration, find the matching SCMI registers for each state
 *        The results might include a count >1 if the metric declaration doesn't specify an instance filter
 *        All results must come from layout members with the same "component" string.
 */
auto FindMatchingScmiRegistersForResidency(astl::metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                           ScmiSpecification const&                          scmi_spec)
    -> std::expected<ResidencyStateRegisterDefinitions, astl_status_code>;

}  // namespace astl::scmi::spec

#endif  // SCMI_PLATFORM_TELEMETRY_SPEC_HPP_
