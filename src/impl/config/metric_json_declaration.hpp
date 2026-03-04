// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef METRIC_JSON_DECLARATION_HPP_
#define METRIC_JSON_DECLARATION_HPP_

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "common/metric_config.hpp"
#include "common/scmi/uuid.hpp"
#include "target.hpp"

// Forward declaration to avoid circular dependency
namespace astl::scmi::spec {
struct ScmiSpecification;
}

namespace astl::metrics::spec {

////////////////////////////////////////////////////////////////////////////////
//  platform_lookup.json parsing                                              //
////////////////////////////////////////////////////////////////////////////////

/** @brief A members of the `scmi_uuid_mapping` table in repometa.json */
struct MetricsDeclarationFileElement {
  std::string last_updated;
  std::string description;
  std::string metrics_file;
};

inline void from_json(const nlohmann::json& json_data, MetricsDeclarationFileElement& metrics_declaration_file) {
  json_data.at("last_updated").get_to(metrics_declaration_file.last_updated);
  json_data.at("description").get_to(metrics_declaration_file.description);
  json_data.at("metrics_file").get_to(metrics_declaration_file.metrics_file);
}

/** @brief A single entry in the platform_lookup json.
 *   Since we have a pattern to match, rather than an exact uuid match,
 *   we can't make this a simple std::map.
 */
struct PlatformLookupEntry {
  scmi::spec::Uuid              uuid;
  MetricsDeclarationFileElement metrics_declaration_file;

  PlatformLookupEntry(scmi::spec::Uuid uuid, MetricsDeclarationFileElement metrics_declaration_file)
      : uuid{std::move(uuid)}, metrics_declaration_file{std::move(metrics_declaration_file)} {}
};

/**
 * @brief represents the platform_lookup.json file structure, mainly the uuid mapping to metric declaration files
 */
struct PlatformLookup {
  std::string                      last_updated;
  std::vector<PlatformLookupEntry> metric_files_by_platform_uuid;
};

inline void from_json(const nlohmann::json& json_data, PlatformLookup& platform_lookup) {
  json_data.at("last_updated").get_to(platform_lookup.last_updated);
  for (const auto& [key, value] : json_data.at("scmi_uuid_mapping").items()) {
    auto uuid = scmi::spec::GetNormalizedUuid(key);
    if (!uuid.has_value()) {
      continue;  // skip invalid uuid entries
    }
    platform_lookup.metric_files_by_platform_uuid.emplace_back(std::move(uuid.value()),
                                                               value.get<MetricsDeclarationFileElement>());
  }
}

/**
 * @brief Find the metrics definition file for a given normalized UUID from the platform_lookup entries.
 * Match only the top-most N bytes of the UUID as specified in the platform_lookup entry.
 */
inline auto FindMetricsFileElementByUuid(const PlatformLookup& platform_lookup, const scmi::spec::Uuid& uuid)
    -> std::optional<MetricsDeclarationFileElement> {
  auto is_matching_uuid = [&uuid](const PlatformLookupEntry& entry) -> bool { return entry.uuid == uuid; };
  auto iter             = std::find_if(std::begin(platform_lookup.metric_files_by_platform_uuid),
                                       std::end(platform_lookup.metric_files_by_platform_uuid), is_matching_uuid);
  if (iter != std::end(platform_lookup.metric_files_by_platform_uuid)) {
    return iter->metrics_declaration_file;
  }
  return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////
//  metric_json_declaration parsing                                           //
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief represents an entry in the the 'collection' element in a metric declaration json
 *        which tells us how to collect this metric, and how to filter and match elements
 *        from the SCMI spec
 */
struct MetricJsonCollectionSettings {
  std::string                protocol;
  std::string                register_name;
  std::optional<std::string> scmi_component_filter;
  std::optional<std::string> scmi_instance_filter;
};

inline void from_json(const nlohmann::json& json_data, MetricJsonCollectionSettings& collection_setting) {
  collection_setting.protocol = json_data.at("protocol").get<std::string>();
  if (json_data.contains("register")) {
    json_data.at("register").get_to(collection_setting.register_name);
  }
  if (json_data.contains("scmi_component_filter")) {
    json_data.at("scmi_component_filter").get_to(collection_setting.scmi_component_filter);
  }
  if (json_data.contains("scmi_instance_filter")) {
    json_data.at("scmi_instance_filter").get_to(collection_setting.scmi_instance_filter);
  }
}

struct MetricJsonDeclaration {
  MetricJsonDeclaration() = default;

  std::string description;  //!< Description of the metric

  //!< Unit of measurement for the metric.  Could be defined in the SCMI spec json instead, in which case this acts as a
  //!< filter.
  std::optional<std::string> unit;

  //!< Type of metric (e.g., value, delta, rate)
  std::string metric_type;

  //!< Categories include things like Temperature, Power, Count, etc.
  std::string category;

  //!< Groups this metric is associated with
  std::optional<std::vector<std::string>> metric_groups;

  //!< Collector types (e.g., scmi, libsensors) and protocol-specific register names and filters
  MetricJsonCollectionSettings collection;

  //!< Optional formula (JSON array format)
  std::optional<nlohmann::json> formula;

  // Residency-specific fields
  std::optional<ResidencyMetricConfig::InferredStateInfo>
                                                       inferred_state;  //!< Inferred state info (for residency metrics)
  std::optional<std::map<std::string, nlohmann::json>> states;          //!< State definitions (for residency metrics)

  // Finite set specific fields
  // Map of label -> {"value": <numeric/bool>, "description": <string>}
  std::optional<std::map<std::string, nlohmann::json>> finite_set_values;  //!< Valid values for finite set metrics
};

inline void from_json(const nlohmann::json& json_data, MetricJsonDeclaration& metric) {
  json_data.at("description").get_to(metric.description);
  if (json_data.contains("unit")) {
    json_data.at("unit").get_to(metric.unit);
  }
  json_data.at("metric_type").get_to(metric.metric_type);
  if (json_data.contains("category")) {
    json_data.at("category").get_to(metric.category);
  } else {
    metric.category = "unknown";  // default if absent
  }

  json_data.at("collection").get_to(metric.collection);

  // Metric groups field is optional
  if (json_data.contains("metric_groups")) {
    metric.metric_groups = json_data["metric_groups"].get<std::vector<std::string>>();
  }

  // Formula field is optional and can be string, object, or array
  if (json_data.contains("formula")) {
    metric.formula = json_data["formula"];  // Store as json (only json array type is supported for this field)
  }

  // Handle residency-specific fields
  // Expected format: "inferred_state": {"name": "C0", "description": "CPU active state"}
  if (json_data.contains("inferred_state")) {
    const auto& inferred_state_json = json_data["inferred_state"];
    metric.inferred_state           = ResidencyMetricConfig::InferredStateInfo{
        inferred_state_json.at("name").get<std::string>(), inferred_state_json.at("description").get<std::string>()};
  }
  if (json_data.contains("states")) {
    metric.states = json_data["states"].get<std::map<std::string, nlohmann::json>>();
  }

  // Handle finite set specific fields
  // Expected format: {"P0": {"value": 0, "description": "..."}, "P1": {"value": 1, "description": "..."}, ...}
  if (json_data.contains("finite_set_values")) {
    metric.finite_set_values = json_data["finite_set_values"].get<std::map<std::string, nlohmann::json>>();
  }
}

/**
 * @brief represents the metric_declarations.json file structure 'document' element for file metadata
 */
struct MetricsDeclarationDocument {
  std::optional<std::string> last_updated;
  std::optional<bool>        confidential;
};

inline void from_json(const nlohmann::json& json_data, MetricsDeclarationDocument& document) {
  if (json_data.contains("last_updated")) {
    json_data.at("last_updated").get_to(document.last_updated);
  }
  if (json_data.contains("confidential")) {
    json_data.at("confidential").get_to(document.confidential);
  }
}

/**
 * @brief represents the metric_declarations.json file structure
 */
struct MetricsDeclaration {
  std::optional<MetricsDeclarationDocument>    document;
  std::map<std::string, MetricJsonDeclaration> metrics;
};

inline void from_json(const nlohmann::json& json_data, MetricsDeclaration& metrics_declaration) {
  if (json_data.contains("document")) {
    json_data.at("document").get_to(metrics_declaration.document);
  }
  json_data.at("metrics").get_to(metrics_declaration.metrics);
}

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType>;

/**
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_key_name    The string key from scmi specification json in the layout.members.<member>. entries list,
 * e.g. 'ENERGY_COUNTER'
 * @param metric_declaration The MetricJsonDeclaration json definition of this type of metric from the astl
 * configuration json. adds info like units on how to interpret the metrics
 * @param scmi_spec          The scmi json spec, especially scmi::Layout for the UUID of the given targets
 * @param targets A vector of Target pointers that all share identical UUIDs on this platform.
 *
 * Expect this to be called once per unique UUID on the platformj:w
 *
 */
[[nodiscard]] auto CreateScmiMetricConfigs(std::string_view                           metric_key_name,
                                           MetricJsonDeclaration const&               metric_declaration,
                                           astl::scmi::spec::ScmiSpecification const& scmi_spec,
                                           std::vector<const ITarget*> const&         applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code>;

}  // namespace astl::metrics::spec

#endif  // METRIC_JSON_DECLARATION_HPP_
