// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_group_json_declaration.hpp"
#include "libsensors/libsensors_metric_builder.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/metric_manager.hpp"
#include "metric/metric_manager_cache_loader.hpp"
#include "metric/procfs_metric_builder.hpp"
#include "metric/scmi_metric_builder.hpp"
#include "metric/scmi_target_configuration.hpp"

namespace astl {

using MetricGroupDescriptionMap = MetricManager::MetricGroupDescriptionMap;

using RegisterMetricsFunction = astl_status_code (*)(
    const AstlConfiguration&, const std::unordered_map<CollectorType, std::vector<const ITarget*>>&, IMetricManager*);

static auto LoadMetricGroupDescriptions(const AstlConfiguration& configuration)
    -> std::expected<MetricGroupDescriptionMap, astl_status_code> {
  const auto metric_group_catalog_path = configuration.groups_dir_path / "metric_groups.json";
  if (!std::filesystem::exists(metric_group_catalog_path)) {
    ASTL_LOG_WARNING(
        "Metric group metadata config not found at {}. "
        "Metrics declaring metric_groups will fail during registration.",
        metric_group_catalog_path.string());
    return MetricGroupDescriptionMap{};
  }

  auto declaration =
      config::TryParseJsonFile<metrics::groups::spec::MetricGroupsDeclaration>(metric_group_catalog_path);
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

static auto RegisterConfiguredMetrics(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code {
  constexpr std::array<RegisterMetricsFunction, 4> register_metrics_functions = {
      RegisterScmiMetrics,
      RegisterLibsensorsMetrics,
      RegisterProcfsMetrics,
      RegisterProcfsCounters,
  };

  astl_status_code status = ASTL_STATUS_SUCCESS;
  for (const auto register_metrics : register_metrics_functions) {
    if (status == ASTL_STATUS_SUCCESS) {
      status = register_metrics(configuration, collector_type_to_targets_map, metric_manager);
    }
  }
  return status;
}

static auto LoadConfiguredMetricManager(const std::vector<std::unique_ptr<ITarget>>& targets,
                                        const std::optional<std::filesystem::path>&  cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  if (!cache_dir_path.has_value()) {
    ASTL_LOG_ERROR("Cache directory path must be provided when load_file_path is specified in configuration");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return LoadMetricManagerFromCache(targets, cache_dir_path.value());
}

auto BuildMetricManager(const std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration,
                        std::optional<std::filesystem::path> cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  if (configuration.load_file_path.has_value()) {
    return LoadConfiguredMetricManager(targets, cache_dir_path);
  }

  auto rename_status = ApplyConfiguredScmiTargetNames(configuration, targets);
  if (rename_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(rename_status);
  }

  // arrange the targets by the collector type
  std::unordered_map<CollectorType, std::vector<const ITarget*>> collector_type_to_targets_map;
  for (const auto& target : targets) {
    collector_type_to_targets_map[target->GetCollectorType()].push_back(target.get());
  }
  // build a vector of CollectorCapability objects for each unique collector
  std::vector<astl::CollectorCapability> collector_caps_list;
  for (const auto& [collector_type, target_list] : collector_type_to_targets_map) {
    if (collector_type == CollectorType::UNKNOWN) {
      ASTL_LOG_ERROR("BuildMetricManager: Found target with UNKNOWN collector type, skipping");
      continue;
    }
    ASTL_LOG_DEBUG("BuildMetricManager: Found {} targets with collector type {}", target_list.size(),
                   static_cast<int>(collector_type));
    collector_caps_list.emplace_back(collector_type);
  }

  // create the astl::Capabilities object to pass to the MetricManager
  astl::SystemCapability              system_capabilities{};
  std::vector<astl::SystemCapability> system_caps_list{system_capabilities};
  astl::Capabilities                  capabilities{std::move(collector_caps_list), std::move(system_caps_list)};

  auto metric_group_descriptions = LoadMetricGroupDescriptions(configuration);
  if (!metric_group_descriptions.has_value()) {
    return std::unexpected(metric_group_descriptions.error());
  }

  std::unique_ptr<astl::IMetricManager> metric_manager =
      std::make_unique<astl::MetricManager>(capabilities, std::move(metric_group_descriptions.value()));

  const auto status = RegisterConfiguredMetrics(configuration, collector_type_to_targets_map, metric_manager.get());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  return metric_manager;
}

}  // namespace astl
