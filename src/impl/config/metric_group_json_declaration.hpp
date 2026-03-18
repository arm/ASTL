// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef METRIC_GROUP_JSON_DECLARATION_HPP_
#define METRIC_GROUP_JSON_DECLARATION_HPP_

#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace astl::metrics::groups::spec {

using json = nlohmann::json;

struct MetricGroupDeclaration {
  std::string description;
};

inline void from_json(const json& json_data, MetricGroupDeclaration& metric_group) {
  json_data.at("description").get_to(metric_group.description);
}

struct MetricGroupsDeclaration {
  std::map<std::string, MetricGroupDeclaration> metric_groups;
};

inline void to_json(json& json_data, const MetricGroupDeclaration& metric_group) {
  json_data["description"] = metric_group.description;
}

inline void from_json(const json& json_data, MetricGroupsDeclaration& declaration) {
  json_data.at("metric_groups").get_to(declaration.metric_groups);
}

}  // namespace astl::metrics::groups::spec

#endif  // METRIC_GROUP_JSON_DECLARATION_HPP_
