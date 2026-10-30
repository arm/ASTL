// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/collector_backend.hpp"
#include "collector/i_collector.hpp"
#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "numa/numa_collector.hpp"
namespace astl {
namespace {
#if defined(ASTL_INCLUDE_NUMA)
class NumaTarget : public Target {
 public:
  std::filesystem::path    root;
  std::vector<std::string> nodes;
  NumaTarget(std::filesystem::path path, std::vector<std::string> instances)
      : Target("numa", "Linux NUMA allocation statistics", CollectorType::NUMA),
        root(std::move(path)),
        nodes(std::move(instances)) {}
};
auto DiscoverNumaTargets(const AstlConfiguration& config) -> std::expected<BackendTargets, astl_status_code> {
  auto nodes = numa::Discover(FileInterface{config.numa_root_path});
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  BackendTargets targets;
  if (!nodes->empty()) {
    targets.push_back(std::make_unique<NumaTarget>(config.numa_root_path, std::move(*nodes)));
  }
  return targets;
}
auto RegisterNumaMetric(const NumaTarget& target, IMetricManager& manager, const std::string& name, NumaField field,
                        const std::string& node = {}) -> astl_status_code {
  const bool        percent = field == NumaField::LOCAL_PERCENT || field == NumaField::REMOTE_PERCENT;
  auto              builder = NumaOperationBuilder{field, node};
  const std::string description =
      percent ? "NUMA interval CPU locality percentage (0-100)" : "NUMA pages allocated during the sampling interval";
  auto metric = std::make_unique<MetricConfig>(name, description, percent ? ASTL_UNITS_PERCENT : ASTL_UNITS_COUNT,
      percent ? ASTL_VALUE_FLOAT64 : ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
      CollectorType::NUMA, builder, percent ? AnyFormula{ScalingFormula{1, numa::kBasisPointsPerPercent}} : AnyFormula{IdentityFormula{}},
      ASTL_VALUE_UINT64, std::vector<std::string>{"memory"}, "numa::" + name);
  auto status = manager.RegisterMetric(std::move(metric), {&target});
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  // Values are already deltas/absolute percentages, never free-running counts.
  const std::string counter_description =
      percent ? "NUMA interval CPU locality percentage (raw basis points)" : description;
  auto counter = std::make_unique<MetricConfig>(
      MetricConfig::RawCounterInfo{name, "numa_counter::" + name, ASTL_COUNTER_TYPE_VALUE}, counter_description,
      ASTL_UNITS_COUNT, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE, CollectorType::NUMA,
      builder);
  return manager.RegisterCounter(std::move(counter), {&target});
}
auto RegisterNumaMetrics(const AstlConfiguration& /*configuration*/, const BackendTargetMap& targets,
                         IMetricManager* manager) -> astl_status_code {
  if (!manager) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto found = targets.find(CollectorType::NUMA);
  if (found == targets.end()) {
    return ASTL_STATUS_SUCCESS;
  }
  for (const auto* target : found->second) {
    const auto* numa = dynamic_cast<const NumaTarget*>(target);
    if (!numa) {
      return ASTL_STATUS_BAD_CONFIGURATION;
    }
    const std::array names{"hit",        "miss",          "interleave_hit", "local_node",
                           "other_node", "local_percent", "remote_percent"};
    for (size_t i = 0; i < names.size(); ++i) {
      const auto status =
          RegisterNumaMetric(*numa, *manager, "Linux_numa_" + std::string{names.at(i)}, static_cast<NumaField>(i));
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
    }
    for (const auto& node : numa->nodes) {
      const auto status =
          RegisterNumaMetric(*numa, *manager, "numa_" + node + "_allocations", NumaField::ALLOCATIONS, node);
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}
#endif
auto BuildNumaCollector(const ITarget& target, const AstlConfiguration& config)
    -> std::expected<std::unique_ptr<ICollector>, astl_status_code> {
  if (config.load_file_path) {
    return nullptr;
  }
#if defined(ASTL_INCLUDE_NUMA)
  const auto* numa = dynamic_cast<const NumaTarget*>(&target);
  if (!numa) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  return std::make_unique<NumaCollector>(FileInterface{numa->root}, numa->nodes);
#else
  (void)target;
  return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
#endif
}
}  // namespace
extern const CollectorBackend kNumaBackend{CollectorType::NUMA, "numa",
#if defined(ASTL_INCLUDE_NUMA)
                                           DiscoverNumaTargets, BuildNumaCollector, RegisterNumaMetrics
#else
                                           nullptr, BuildNumaCollector, nullptr
#endif
};
}  // namespace astl
