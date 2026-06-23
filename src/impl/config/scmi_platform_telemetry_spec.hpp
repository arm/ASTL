// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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

/** @brief A single entry in the repometa.json uuid_mapping table
 *
 * Will look something like:
 * "7AC0CA75-D07E-EA75-8F80-EB2CF9410000/14": {
 *   "last_updated": "2026-01-28",
 *   "description": "Example snippet",
 *   "specification_file": "mock/metrics.json"
 * }
 *
 * resulting in uuid (normalized): 7ac0ca75d07eea758f80eb2cf9410000
 * and num_significant_bytes: 14
 *
 */
struct RepoMetaEntry {
  scmi::spec::Uuid  uuid;
  TelemetrySpecFile spec_file;

  RepoMetaEntry(scmi::spec::Uuid uuid, TelemetrySpecFile spec_file)
      : uuid(std::move(uuid)), spec_file(std::move(spec_file)) {}
};

/**
 * @brief represents the repometa.json file structure, mainly the uuid mapping to spec files
 */
struct RepoMeta {
  std::string                last_updated;
  std::vector<RepoMetaEntry> spec_files_by_uuid;
};

inline void from_json(const json& json_data, RepoMeta& repo_meta) {
  json_data.at("last_updated").get_to(repo_meta.last_updated);
  const auto& uuid_mapping = json_data.at("uuid_mapping");
  repo_meta.spec_files_by_uuid.reserve(uuid_mapping.size());

  for (const auto& [key, value] : uuid_mapping.items()) {
    auto uuid = GetNormalizedUuid(key);
    if (!uuid.has_value()) {
      continue;  // skip invalid uuid entries
    }
    repo_meta.spec_files_by_uuid.emplace_back(std::move(uuid.value()), value.get<TelemetrySpecFile>());
  }
}

/**
 * @brief Find the specification file for a given normalized UUID from the repometa entries.
 * Match only the top-most N bytes of the UUID as specified in the repometa entry.
 * If multiple entries match, the match with the longest significant byte match (i.e. most specific) will be returned.
 */
inline auto FindSpecFileByUuid(const RepoMeta& repo_meta, const scmi::spec::Uuid& uuid)
    -> std::optional<TelemetrySpecFile> {
  const RepoMetaEntry* best_match = nullptr;
  for (const auto& entry : repo_meta.spec_files_by_uuid) {
    if (entry.uuid != uuid) {
      continue;
    }
    if (best_match == nullptr || entry.uuid.num_significant_bytes > best_match->uuid.num_significant_bytes) {
      best_match = &entry;
    }
  }
  if (best_match) {
    return best_match->spec_file;
  }
  return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////
//  metrics_specification parsing (e.g. scp.json, lcp_cluster.json            //
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief A single member of []members[n].metrics[]
 */
struct DataEvent {
  uint32_t    base_de_id{};
  std::string name;
  std::string component;
  std::string description;
  std::string unit;
  int32_t     base10_unit_modifier{};
  uint16_t    rel_offset{};
};

inline void from_json(const json& json_data, DataEvent& entry) {
  entry.base_de_id = ParseToUint32(json_data, "base_de_id");
  json_data.at("name").get_to(entry.name);
  json_data.at("component").get_to(entry.component);
  json_data.at("description").get_to(entry.description);
  json_data.at("unit").get_to(entry.unit);
  if (json_data.contains("base10_unit_modifier")) {
    json_data.at("base10_unit_modifier").get_to(entry.base10_unit_modifier);
  } else {
    entry.base10_unit_modifier = 0;
  }
  entry.rel_offset = ParseToUint16(json_data, "rel_offset");
}

/**
 * @brief A single member of the json metrics definitions file for scmi
 *
 * @example
 * "members": [
 *    {
 *       "count": 6,
 *       "component_type": "0x94",
 *       "component_name": "PSS",
 *       "start_offset": 40,
 *       "block_size": 48,
 *       "metrics": {
 *          "TEMP_PRESENT": {
 *             "description": "Temperature Present",
 *             "type": "Gauge",
 *             "unit": "celsius",
 *             "base10_unit_modifier": -3,
 *             "name": "TEMP_PRESENT",
 *             "component": "PSS",
 *             "base_de_id": "0x00009441",
 *             "line_size": 16,
 *             "rel_offset": "0x0028"
 *          },
 *          "TEMP_MINIMUM_1M": {
 *             "description": "Temperature Minimum 1 Minute",
 *             "type": "Gauge",
 *             "unit": "celsius",
 *             "base10_unit_modifier": -3,
 *             "name": "TEMP_MINIMUM_1M",
 *             "component": "PSS",
 *             "base_de_id": "0x00009442",
 *             "line_size": 16,
 *             "rel_offset": "0x0038"
 *          },
 *       }
 *    }
 *
 * represents 12 data events, including
 * - PSS.0.TEMP_PRESENT      at data event 0x0000_9441
 * - PSS.0.TEMP_MINIMUM_1M   at data event 0x0000_9442
 * - PSS.1.TEMP_PRESENT      at data event 0x0001_9441
 * - PSS.1.TEMP_MINIMUM_1M   at data event 0x0001_9442
 * - ...
 * - PSS.5.TEMP_PRESENT      at data event 0x0005_9441
 * - PSS.5.TEMP_MINIMUM_1M   at data event 0x0005_9442
 */
struct Member {
  uint32_t                         count{};
  uint64_t                         start_offset{};
  uint64_t                         block_size{};
  std::map<std::string, DataEvent> metrics;
};

inline void from_json(const json& json_data, Member& member) {
  json_data.at("count").get_to(member.count);
  json_data.at("start_offset").get_to(member.start_offset);
  json_data.at("block_size").get_to(member.block_size);
  json_data.at("metrics").get_to(member.metrics);
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
  std::string           tdcf_instance_id;
  std::string           chiplet_id;
  uint32_t              size{};
  std::vector<Member>   members;
  // map of component instance name to alias name, e.g. "VOLTAGE_RAIL.0" -> "VCPU_C0"
  std::unordered_map<std::string, std::string> aliases;
};

inline void from_json(const json& json_data, ScmiSpecification& spec) {
  if (json_data.contains("_type")) {
    json_data.at("_type").get_to(spec._type);
  } else {
    spec._type = "";
  }
  if (json_data.contains("document")) {
    json_data.at("document").get_to(spec.specification_document);
  }
  json_data.at("uuid").get_to(spec.uuid);
  json_data.at("description").get_to(spec.description);
  if (json_data.contains("tdcf_instance_id")) {
    json_data.at("tdcf_instance_id").get_to(spec.tdcf_instance_id);
  } else {
    spec.tdcf_instance_id = "";
  }
  if (json_data.contains("chiplet_id")) {
    json_data.at("chiplet_id").get_to(spec.chiplet_id);
  } else {
    spec.chiplet_id = "";
  }
  json_data.at("size").get_to(spec.size);
  json_data.at("members").get_to(spec.members);
  if (json_data.contains("aliases")) {
    json_data.at("aliases").get_to(spec.aliases);
  }
}

/**
 * @brief POD class to hold the relevant parts of a metric declaration and SCMI spec entry
 */
struct ScmiMetricDeclaration {
  std::string     name;
  std::string     component;
  std::string     instance;
  astl_units_t    units;
  int32_t         base10_unit_modifier{};
  ScmiDataEventId de_id{};

  ScmiMetricDeclaration(std::string name, std::string component, std::string instance, astl_units_t units,
                        int32_t base10_unit_modifier, ScmiDataEventId de_id)
      : name(std::move(name)),
        component(std::move(component)),
        instance(std::move(instance)),
        units(units),
        base10_unit_modifier(base10_unit_modifier),
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
 *
 * The SCMI JSON spec's `count` field expresses the number of repeated instances per *telemetry target*
 * (i.e. per `tlm-N` sysfs directory). For multi-target platforms the metric instance numbering is
 * globally unique across targets, computed as `global_instance = target_index * count + local_instance`,
 * and the data event IDs are derived from that same global instance index (encoded in the upper 16 bits
 * of the DE id). This mirrors the SCP/LCP cluster layout where, e.g., PSS instances 0..2 live in `tlm-0`
 * (DE ids 0..2) and 3..5 live in `tlm-1` (DE ids 3..5).
 *
 * @param metric_declaration The generic metric declaration to match against
 * @param scmi_specification The Scmi specification containing the Data Event IDs
 * @param target_index Zero-based index of the target (within the ordered list of targets sharing the
 *        same SCMI UUID) for which to generate per-target instance labels. Defaults to 0 for
 *        single-target callers/tests.
 * @return A collection of ScmiMetricDeclaration entries listing all registers matching generic 'register_name'
 *         and details like the specific component+instance name (e.g. PSS_BMU.1.ENERGY_COUNTER) and DE_ID
 */
auto GetMetricRegistersScmiData(astl::metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                ScmiSpecification const& scmi_specification, std::size_t target_index = 0)
    -> std::vector<ScmiMetricDeclaration>;

/**
 * @brief A helper to hold SCMI json spec data matching a residency metric's required registers
 */
struct ResidencyStateRegisterDefinitions {
  struct StateRegisterDefinition {
    // Per-state base DE_ID before instance offset is applied.
    ScmiDataEventId base_data_event_id{};
    // Preserved so residency can use the same post-collection scaling path in follow-up work.
    int32_t base10_unit_modifier{};
  };

  // number of instances of the members in the matching scmi layout member
  // note that these registers in `state_to_register_def` might come from different layout members.
  // When matching these registers, it's simply an error if their layout blocks have different 'count' values.
  std::size_t count{0};
  std::string component;  // component name from the scmi spec
  // map of state names (from the residency metric declaration) to SCMI register metadata.
  std::unordered_map<std::string, StateRegisterDefinition> state_to_register_def;
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
