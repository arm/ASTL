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

#ifndef SCMI_SPECIFICATION_JSON_HPP_
#define SCMI_SPECIFICATION_JSON_HPP_

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/scmi/scmi_read_operation.hpp"

using json = nlohmann::json;

namespace astl {
namespace scmi {

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

// Common structures

struct ConfigRegister {
  std::string                description;
  std::string                offset;
  uint64_t                   size{};
  std::string                default_value;
  std::optional<std::string> addr;  // only present under datasources
};

// deserialize both kinds of config-register JSON
inline void from_json(const json& json_data, ConfigRegister& reg) {
  json_data.at("description").get_to(reg.description);
  json_data.at("offset").get_to(reg.offset);
  json_data.at("size").get_to(reg.size);
  json_data.at("default").get_to(reg.default_value);
  if (json_data.contains("addr")) {
    reg.addr = json_data.at("addr").get<std::string>();
  }
}

struct LayoutMemberEntry {
  uint32_t        instance_id{};
  uint32_t        local_id{};
  uint64_t        offset{};
  std::string     name;
  uint64_t        line_addr{};
  uint64_t        value_addr{};
  ScmiDataEventId de_id{};
};

inline void from_json(const json& json_data, LayoutMemberEntry& entry) {
  json_data.at("instance_id").get_to(entry.instance_id);
  json_data.at("local_id").get_to(entry.local_id);
  entry.offset = ParseToUint64(json_data, "offset");
  json_data.at("name").get_to(entry.name);
  entry.line_addr  = ParseToUint64(json_data, "offset");
  entry.value_addr = ParseToUint64(json_data, "value_addr");
  entry.de_id      = ParseToUint32(json_data, "de_id");
}

struct ProcessSrc {
  std::string description;
  uint64_t    offset{};
  uint64_t    size{};
  std::string name;
  uint64_t    addr{};
};

inline void from_json(const json& json_data, ProcessSrc& src) {
  json_data.at("description").get_to(src.description);
  src.offset = ParseToUint64(json_data, "offset");
  json_data.at("size").get_to(src.size);
  json_data.at("name").get_to(src.name);
  src.addr = ParseToUint64(json_data, "addr");
}

////////////////////////////////////////////////////////////////////////////////
// define structures representing `definitions`
////////////////////////////////////////////////////////////////////////////////

struct DataSourceDefinition {
  std::string                           source_type;
  std::string                           description;
  std::vector<std::string>              required_ext_parameters;
  std::string                           cfg_address_calc;
  std::string                           smcf_address_calc;
  std::map<std::string, ConfigRegister> config_registers;
};

inline void from_json(const json& json_data, DataSourceDefinition& definition) {
  json_data.at("source_type").get_to(definition.source_type);
  json_data.at("description").get_to(definition.description);
  // comma-separated list
  {
    std::string        tmp = json_data.at("_required_ext_parameters").get<std::string>();
    std::istringstream tokenizer(tmp);
    while (std::getline(tokenizer, tmp, ',')) {
      definition.required_ext_parameters.push_back(tmp);
    }
  }
  if (json_data.contains("_cfg_address_calc")) {
    json_data.at("_cfg_address_calc").get_to(definition.cfg_address_calc);
  }
  if (json_data.contains("_smcf_address_calc")) {
    json_data.at("_smcf_address_calc").get_to(definition.smcf_address_calc);
  }
  json_data.at("config_registers").get_to(definition.config_registers);
}

struct Definitions {
  std::string                                                        description;
  std::map<std::string, std::map<std::string, DataSourceDefinition>> groups;
};

inline void from_json(const json& json_data, Definitions& defs) {
  json_data.at("_description").get_to(defs.description);
  for (const auto& [key, value] : json_data.items()) {
    if (key.starts_with('_')) {
      continue;
    }
    defs.groups[key] = value.get<std::map<std::string, DataSourceDefinition>>();
  }
}

////////////////////////////////////////////////////////////////////////////////
// define structures representing `transformations`
////////////////////////////////////////////////////////////////////////////////

// Single transformation entry: may have optional count, plus src and dest
struct TransformationEntry {
  std::optional<int> count;  // only present for max*n entries
  ProcessSrc         src;    // reuse ProcessSrc (src block)
  LayoutMemberEntry  dest;   // reuse LayoutMemberEntry (dest block)
};

inline void from_json(const json& json_data, TransformationEntry& entry) {
  if (json_data.contains("count")) {
    entry.count = json_data.at("count").get<int>();
  }
  json_data.at("src").get_to(entry.src);
  json_data.at("dest").get_to(entry.dest);
}

// Map of operation name -> entry
using Transformations = std::map<std::string, TransformationEntry>;

////////////////////////////////////////////////////////////////////////////////
// define structures representing `datasources`
////////////////////////////////////////////////////////////////////////////////

struct SmcfSource {
  std::string                           source_type;
  std::string                           description;
  std::map<std::string, ConfigRegister> config_registers;
};

inline void from_json(const json& json_data, SmcfSource& src) {
  json_data.at("source_type").get_to(src.source_type);
  json_data.at("description").get_to(src.description);
  json_data.at("config_registers").get_to(src.config_registers);
}

struct TelemetrySource {
  std::string                       name;
  std::string                       description;
  int                               index{};
  std::map<std::string, SmcfSource> smcf_sources;
};

inline void from_json(const json& json_data, TelemetrySource& src) {
  json_data.at("name").get_to(src.name);
  json_data.at("description").get_to(src.description);
  json_data.at("index").get_to(src.index);
  json_data.at("smcf_sources").get_to(src.smcf_sources);
}

struct DataSource {
  std::string                            base_uuid;
  uint64_t                               base_address{};
  uint64_t                               config_size{};
  uint64_t                               mgi_base_address{};
  uint64_t                               mgi_config_size{};
  uint64_t                               smcf_container_base_addr{};
  uint64_t                               smcf_container_size{};
  std::map<std::string, TelemetrySource> sources;
};

inline void from_json(const json& json_data, DataSource& datasource) {
  json_data.at("base_uuid").get_to(datasource.base_uuid);
  datasource.base_address             = ParseToUint64(json_data, "base_address");
  datasource.config_size              = ParseToUint64(json_data, "config_size");
  datasource.mgi_base_address         = ParseToUint64(json_data, "mgi_base_address");
  datasource.mgi_config_size          = ParseToUint64(json_data, "mgi_config_size");
  datasource.smcf_container_base_addr = ParseToUint64(json_data, "smcf_container_base_addr");
  datasource.smcf_container_size      = ParseToUint64(json_data, "smcf_container_size");
  json_data.at("sources").get_to(datasource.sources);
}

using DataSources = std::map<std::string, DataSource>;

////////////////////////////////////////////////////////////////////////////////
// define structures representing `processes`
////////////////////////////////////////////////////////////////////////////////

// ----------------
// Define the copy operation (e.g. “copy_long”)
// ----------------

struct CopyOperation {
  ProcessSrc        src;
  LayoutMemberEntry dest;
};

inline void from_json(const json& json_data, CopyOperation& operation) {
  json_data.at("src").get_to(operation.src);
  json_data.at("dest").get_to(operation.dest);
}

// ----------------
// Alias the nested maps for clarity
// ----------------

using ProcessOperations = std::map<std::string, CopyOperation>;      // e.g. "copy_long" -> CopyOperation
using MetricProcesses   = std::map<std::string, ProcessOperations>;  // e.g. "CPU_CYCLES" -> ProcessOperations
using Processes         = std::map<std::string, MetricProcesses>;    // e.g. "AP0" -> MetricProcesses

////////////////////////////////////////////////////////////////////////////////
// define structures representing `layout`
////////////////////////////////////////////////////////////////////////////////

struct Layout {
  uint64_t base_addr{};
  // members: e.g. "AP0" -> map of registerName -> LayoutMemberEntry
  std::map<std::string, std::map<std::string, LayoutMemberEntry>> members;
};

inline void from_json(const json& json_data, Layout& layout) {
  std::string string_for_conversion;
  json_data.at("base_addr").get_to(string_for_conversion);
  layout.base_addr = std::stoull(string_for_conversion, nullptr, 0);
  // build the nested map from each member block
  for (const auto& [memberName, memberObj] : json_data.at("members").items()) {
    layout.members[memberName] = memberObj.get<std::map<std::string, LayoutMemberEntry>>();
  }
}

////////////////////////////////////////////////////////////////////////////////
// define root structure 'ScmiSpecification' tying it all together
////////////////////////////////////////////////////////////////////////////////
struct ScmiSpecification {
  Definitions     definitions;
  Transformations transformations;
  DataSources     datasources;
  Processes       processes;
  Layout          layout;
};

inline void from_json(const json& json_data, ScmiSpecification& root) {
  root.definitions = json_data.at("definitions").get<Definitions>();
  // TODO(ASTL-40) --  disabling this for now
  // because the '_descriptions' element of transformations doesn't fit well with our schema that expects
  // transformations to have a set of maps under it
  // root.transformations = json_data.at("transformations").get<Transformations>();
  root.datasources = json_data.at("datasources").get<DataSources>();
  root.processes   = json_data.at("processes").get<Processes>();
  root.layout      = json_data.at("layout").get<Layout>();
}

inline auto GetDataEventIdForLayoutMember(std::string_view register_name, std::string_view member_name,
                                          std::map<std::string, LayoutMemberEntry> const& registers)
    -> std::optional<ScmiDataEventId> {
  for (const auto& [metric_type_name, layout_member] : registers) {
    // note, we're using the key name for the register, e.g. 'CPU_CYCLES',
    // not the .name field of the entry, e.g. 'AP0_CPU_CYCLES', so that one
    // metric can be defined in the library configuration file, and be created
    // for each target that supports it in the spec.
    if (metric_type_name == register_name) {
      ASTL_LOG_TRACE("GetDataEventIdForLayoutMember matched '{}' with '{}' for member {}", metric_type_name,
                     register_name, member_name);
      return layout_member.de_id;
    }
  }
  return std::nullopt;
}

/**
 * @brief Get the collection of metric names (i.e. AP0_ENERGY_COUNTER) that match the given register name, and their
 * corresponding data event ids.
 * @param register_name The register name to look up (i.e. ENERGY_COUNTER)
 * @param layout The Scmi layout specification containing the Data Event IDs from platform json spec
 * @return A map of target names to Data Event IDs for the metric
 */
inline auto GetMetricRegisters(std::string_view register_name,
                               Layout const&    layout) -> std::vector<std::pair<std::string, ScmiDataEventId>> {
  std::vector<std::pair<std::string, ScmiDataEventId>> metric_names_and_de_id;

  for (const auto& [member_name, metrics] : layout.members) {  // e.g. AP0, AP1
    for (const auto& [metric_type_name, metric_entry] : metrics) {
      // note, we're using the key name for the register, e.g. 'CPU_CYCLES',
      // not the .name field of the entry, e.g. 'AP0_CPU_CYCLES', so that one
      // metric can be defined in the library configuration file, and be created
      // for each target that supports it in the spec.
      if (metric_type_name == register_name) {
        ASTL_LOG_TRACE("GetMetricRegisters matched '{}' (from {}) with '{}' for member {}", metric_type_name,
                       metric_entry.name, register_name, member_name);
        metric_names_and_de_id.emplace_back(metric_entry.name, metric_entry.de_id);
      }
    }
  }
  return metric_names_and_de_id;
}

}  // namespace scmi

}  // namespace astl

#endif  // SCMI_SPECIFICATION_JSON_HPP_
