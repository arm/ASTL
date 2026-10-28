// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/metric_builder.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_group_json_declaration.hpp"
#include "metric/metric_manager.hpp"
#include "metric/metric_manager_cache_loader.hpp"

namespace astl {

using MetricGroupDescriptionMap = MetricManager::MetricGroupDescriptionMap;

static auto LoadMetricGroupDescriptions(const AstlConfiguration& configuration)
    -> std::expected<MetricGroupDescriptionMap, astl_status_code> {
  const auto metric_group_catalog_path = configuration.groups_dir_path / "metric_groups.json";
  if (!configuration.resources->Exists(metric_group_catalog_path)) {
    ASTL_LOG_WARNING(
        "Metric group metadata config not found at {}. "
        "Metrics declaring metric_groups will fail during registration.",
        metric_group_catalog_path.string());
    return MetricGroupDescriptionMap{};
  }

  auto declaration = config::TryParseJsonFile<metrics::groups::spec::MetricGroupsDeclaration>(
      metric_group_catalog_path, configuration.resources.get());
  if (!declaration.has_value()) {
    return std::unexpected(declaration.error());
  }

  MetricGroupDescriptionMap descriptions;
  descriptions.reserve(declaration->metric_groups.size());
  for (const auto& [group_name, group_declaration] : declaration->metric_groups) {
    descriptions.emplace(group_name, group_declaration.description);
  }
  return descriptions;
}

auto BuildMetricManager(const std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration,
                        std::optional<std::filesystem::path> cache_dir_path, CollectorBackends backends)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  if (configuration.load_file_path.has_value()) {
    if (!cache_dir_path) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return LoadMetricManagerFromCache(targets, *cache_dir_path);
  }

  if (auto status = ValidateCollectorBackends(backends); status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  for (const auto& backend : backends) {
    if (backend.prepare_targets != nullptr) {
      const auto status = backend.prepare_targets(configuration, targets);
      if (status != ASTL_STATUS_SUCCESS) {
        return std::unexpected(status);
      }
    }
  }

  BackendTargetMap                 collector_type_to_targets_map;
  std::vector<CollectorCapability> collector_caps;
  for (const auto& target : targets) {
    const auto type        = target->GetCollectorType();
    auto [entry, inserted] = collector_type_to_targets_map.try_emplace(type);
    entry->second.push_back(target.get());
    if (inserted && type != CollectorType::UNKNOWN) {
      collector_caps.emplace_back(type);
    }
  }
  Capabilities capabilities{std::move(collector_caps), {SystemCapability{}}};

  auto metric_group_descriptions = LoadMetricGroupDescriptions(configuration);
  if (!metric_group_descriptions.has_value()) {
    return std::unexpected(metric_group_descriptions.error());
  }

  std::unique_ptr<astl::IMetricManager> metric_manager =
      std::make_unique<astl::MetricManager>(capabilities, std::move(metric_group_descriptions.value()));

  for (const auto& backend : backends) {
    if (backend.register_metrics == nullptr) {
      continue;
    }
    const auto status = backend.register_metrics(configuration, collector_type_to_targets_map, metric_manager.get());
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
  }
  return metric_manager;
}

}  // namespace astl
