// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef METRIC_JSON_DECLARATION_HPP_
#define METRIC_JSON_DECLARATION_HPP_

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "common/metric_config.hpp"

namespace astl::metrics::spec {

////////////////////////////////////////////////////////////////////////////////
//  metric_json_declaration parsing                                           //
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Shared `collection` fields from a metric declaration json.
 *
 * Protocol-specific fields stay in `raw_json` so they can be parsed by
 * protocol-specific helpers instead of accumulating in this shared type.
 */
struct MetricJsonCollectionSettings {
  std::string                protocol;
  std::string                register_name;
  std::optional<std::string> scmi_component_filter;
  std::optional<std::string> scmi_instance_filter;
  nlohmann::json             raw_json;
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
  collection_setting.raw_json = json_data;
}

struct DerivedMetricJsonDeclaration {
  std::optional<std::string>              description;
  std::optional<std::string>              register_suffix;
  std::optional<std::string>              unit;
  std::optional<std::string>              identifier;
  std::optional<std::string>              metric_type;
  std::optional<std::vector<std::string>> metric_groups;
};

inline void from_json(const nlohmann::json& json_data, DerivedMetricJsonDeclaration& derived_metric) {
  if (json_data.is_string()) {
    derived_metric.description = json_data.get<std::string>();
    return;
  }
  if (json_data.is_object()) {
    if (json_data.contains("description")) {
      json_data.at("description").get_to(derived_metric.description);
    }
    if (json_data.contains("register_suffix")) {
      json_data.at("register_suffix").get_to(derived_metric.register_suffix);
    }
    if (json_data.contains("unit")) {
      json_data.at("unit").get_to(derived_metric.unit);
    }
    if (json_data.contains("identifier")) {
      json_data.at("identifier").get_to(derived_metric.identifier);
    }
    if (json_data.contains("metric_type")) {
      json_data.at("metric_type").get_to(derived_metric.metric_type);
    }
    if (json_data.contains("metric_groups")) {
      derived_metric.metric_groups = json_data.at("metric_groups").get<std::vector<std::string>>();
    }
  }
}

struct MetricJsonDeclaration {
  MetricJsonDeclaration() = default;

  std::string description;  //!< Description of the metric

  //!< Unit of measurement for the metric. Could be defined in the SCMI spec json instead, in which case this acts as a
  //!< filter or output-unit override when paired with an explicit formula.
  std::optional<std::string> unit;
  std::optional<std::string> value_type;

  //!< Type of metric (e.g., value, delta, rate)
  std::string metric_type;

  //!< High-level metric identifier such as Temperature, Power, Count, etc.
  std::string identifier;

  //!< Groups this metric is associated with
  std::optional<std::vector<std::string>> metric_groups;
  MetricJsonCollectionSettings            collection;
  std::optional<nlohmann::json>           formula;

  std::optional<ResidencyMetricConfig::InferredStateInfo>            inferred_state;
  std::optional<std::map<std::string, nlohmann::json>>               states;
  std::optional<std::map<std::string, nlohmann::json>>               finite_set_values;
  std::optional<std::map<std::string, DerivedMetricJsonDeclaration>> derived_metrics;
};

inline void from_json(const nlohmann::json& json_data, MetricJsonDeclaration& metric) {
  json_data.at("description").get_to(metric.description);
  if (json_data.contains("unit")) {
    json_data.at("unit").get_to(metric.unit);
  }
  if (json_data.contains("value_type")) {
    json_data.at("value_type").get_to(metric.value_type);
  }
  json_data.at("metric_type").get_to(metric.metric_type);
  if (json_data.contains("identifier")) {
    json_data.at("identifier").get_to(metric.identifier);
  }
  if (metric.identifier.empty()) {
    metric.identifier = "unknown";
  }

  json_data.at("collection").get_to(metric.collection);

  if (json_data.contains("metric_groups")) {
    metric.metric_groups = json_data["metric_groups"].get<std::vector<std::string>>();
  }

  if (json_data.contains("formula")) {
    metric.formula = json_data["formula"];
  }

  if (json_data.contains("inferred_state")) {
    const auto& inferred_state_json = json_data["inferred_state"];
    metric.inferred_state           = ResidencyMetricConfig::InferredStateInfo{
        inferred_state_json.at("name").get<std::string>(), inferred_state_json.at("description").get<std::string>()};
  }
  if (json_data.contains("states")) {
    metric.states = json_data["states"].get<std::map<std::string, nlohmann::json>>();
  }

  if (json_data.contains("finite_set_values")) {
    metric.finite_set_values = json_data["finite_set_values"].get<std::map<std::string, nlohmann::json>>();
  }
  if (json_data.contains("derived_metrics")) {
    metric.derived_metrics = json_data["derived_metrics"].get<std::map<std::string, DerivedMetricJsonDeclaration>>();
  }
}

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

struct MetricsDeclaration {
  std::optional<MetricsDeclarationDocument>    document;
  std::map<std::string, MetricJsonDeclaration> metrics;
  std::optional<std::string>                   extends;
};

inline void from_json(const nlohmann::json& json_data, MetricsDeclaration& metrics_declaration) {
  if (json_data.contains("document")) {
    json_data.at("document").get_to(metrics_declaration.document);
  }
  if (json_data.contains("extends")) {
    json_data.at("extends").get_to(metrics_declaration.extends);
  }
  json_data.at("metrics").get_to(metrics_declaration.metrics);
}

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType>;

}  // namespace astl::metrics::spec

#endif  // METRIC_JSON_DECLARATION_HPP_
