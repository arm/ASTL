// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_METRIC_JSON_DECLARATION_HPP_
#define SCMI_METRIC_JSON_DECLARATION_HPP_

#include <algorithm>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "astl/astl_errors.h"
#include "common/metric_config.hpp"
#include "common/scmi/uuid.hpp"
#include "config/metric_json_declaration.hpp"
#include "target.hpp"

namespace astl::scmi::spec {
struct ScmiSpecification;
}

namespace astl::metrics::spec {

struct MetricsDeclarationFileElement {
  std::string                last_updated;
  std::string                description;
  std::string                metrics_file;
  std::filesystem::path      resolved_metrics_file;
  std::optional<std::string> name;
};

inline void from_json(const nlohmann::json& json_data, MetricsDeclarationFileElement& metrics_declaration_file) {
  json_data.at("last_updated").get_to(metrics_declaration_file.last_updated);
  json_data.at("description").get_to(metrics_declaration_file.description);
  json_data.at("metrics_file").get_to(metrics_declaration_file.metrics_file);
  if (json_data.contains("name")) {
    json_data.at("name").get_to(metrics_declaration_file.name);
  }
}

struct PlatformLookupEntry {
  scmi::spec::Uuid              uuid;
  MetricsDeclarationFileElement metrics_declaration_file;

  PlatformLookupEntry(scmi::spec::Uuid uuid, MetricsDeclarationFileElement metrics_declaration_file)
      : uuid{std::move(uuid)}, metrics_declaration_file{std::move(metrics_declaration_file)} {}
};

struct PlatformLookup {
  std::string                      last_updated;
  std::vector<PlatformLookupEntry> metric_files_by_platform_uuid;
};

inline void from_json(const nlohmann::json& json_data, PlatformLookup& platform_lookup) {
  json_data.at("last_updated").get_to(platform_lookup.last_updated);
  for (const auto& [key, value] : json_data.at("scmi_uuid_mapping").items()) {
    auto uuid = scmi::spec::GetNormalizedUuid(key);
    if (!uuid.has_value()) {
      continue;
    }
    platform_lookup.metric_files_by_platform_uuid.emplace_back(std::move(uuid.value()),
                                                               value.get<MetricsDeclarationFileElement>());
  }
}

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

struct ScmiMetricJsonCollectionSettings {
  std::string                register_name;
  std::optional<std::string> scmi_component_filter;
  std::optional<std::string> scmi_instance_filter;
};

[[nodiscard]] auto ParseScmiMetricJsonCollectionSettings(const MetricJsonCollectionSettings& collection_setting)
    -> std::expected<ScmiMetricJsonCollectionSettings, astl_status_code>;

auto BuildScalingFormulaFromBase10Modifier(int32_t base10_unit_modifier) -> AnyFormula;

[[nodiscard]] auto CreateScmiMetricConfigs(std::string_view                           metric_key_name,
                                           MetricJsonDeclaration const&               metric_declaration,
                                           astl::scmi::spec::ScmiSpecification const& scmi_spec,
                                           std::vector<const ITarget*> const&         applicable_targets)
    -> std::expected<MetricConfigOnTargets, astl_status_code>;

}  // namespace astl::metrics::spec

#endif  // SCMI_METRIC_JSON_DECLARATION_HPP_
