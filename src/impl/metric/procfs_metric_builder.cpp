// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/procfs_metric_builder.hpp"

#include <memory>
#include <vector>

#include "astl_logger.hpp"
#include "common/procfs_utils.hpp"
#include "metric/formula_builder.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/procfs_metric_builder_helpers.hpp"
#include "operation/procfs_operation_builder.hpp"
#include "topology/procfs_target.hpp"

namespace astl {

namespace {

auto GetProcfsRootPath(const ITarget* target) -> std::filesystem::path {
  if (const auto* procfs_target = dynamic_cast<const ProcfsTarget*>(target)) {
    return procfs_target->ProcfsRootPath();
  }
  return procfs::kDefaultProcfsRootPath;
}

auto MakeFormula(const procfs::MetricDescriptor& descriptor) -> AnyFormula {
  if (descriptor.scale_numerator == 1 && descriptor.scale_denominator == 1) {
    return AnyFormula{IdentityFormula{}};
  }
  return AnyFormula{
      ScalingFormula{descriptor.scale_numerator, descriptor.scale_denominator}
  };
}

auto RegisterProcfsMetricsForTarget(
    const std::vector<std::pair<std::string, metrics::spec::MetricJsonDeclaration>>& procfs_metric_declarations,
    IMetricManager* metric_manager, const ITarget* target, const FileInterface& procfs_file_interface)
    -> astl_status_code {
  for (const auto& [metric_name, metric_declaration] : procfs_metric_declarations) {
    auto metric_configs = procfs_metric_builder_helpers::CreateProcfsMetricConfigs(metric_name, metric_declaration,
                                                                                   target, procfs_file_interface);
    if (!metric_configs.has_value()) {
      return metric_configs.error();
    }

    for (auto& metric_config : *metric_configs) {
      const auto status = metric_manager->RegisterMetric(std::move(metric_config), {target});
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
    }
  }

  return ASTL_STATUS_SUCCESS;
}

}  // namespace

auto RegisterProcfsMetrics(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code {
  if (!metric_manager) {
    ASTL_LOG_ERROR("RegisterProcfsMetrics: metric_manager is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  const auto procfs_targets_it = collector_type_to_targets_map.find(CollectorType::PROCFS);
  if (procfs_targets_it == collector_type_to_targets_map.end()) {
    ASTL_LOG_INFO("No targets with PROCFS collector type found, skipping PROCFS metric registration");
    return ASTL_STATUS_SUCCESS;
  }

  auto procfs_metric_declarations = procfs_metric_builder_helpers::LoadProcfsMetricDeclarations(configuration);
  if (!procfs_metric_declarations.has_value()) {
    return procfs_metric_declarations.error();
  }

  for (const auto* target : procfs_targets_it->second) {
    const FileInterface procfs_file_interface{GetProcfsRootPath(target)};
    const auto          status =
        RegisterProcfsMetricsForTarget(*procfs_metric_declarations, metric_manager, target, procfs_file_interface);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto RegisterProcfsCounters(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code {
  (void)configuration;
  if (!metric_manager) {
    ASTL_LOG_ERROR("RegisterProcfsCounters: metric_manager is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  const auto procfs_targets_it = collector_type_to_targets_map.find(CollectorType::PROCFS);
  if (procfs_targets_it == collector_type_to_targets_map.end()) {
    ASTL_LOG_INFO("No targets with PROCFS collector type found, skipping PROCFS counter registration");
    return ASTL_STATUS_SUCCESS;
  }

  for (const auto* target : procfs_targets_it->second) {
    FileInterface procfs_file_interface{GetProcfsRootPath(target)};
    auto          descriptors = procfs::DiscoverCounterDescriptors(procfs_file_interface);
    if (!descriptors.has_value()) {
      return descriptors.error();
    }

    for (const auto& descriptor : *descriptors) {
      auto counter_config = std::make_unique<MetricConfig>(
          descriptor.metric_name, descriptor.description, descriptor.units, descriptor.value_type,
          descriptor.identifier, ASTL_METRIC_VALUE, CollectorType::PROCFS,
          ProcfsOperationBuilder{descriptor.field_descriptor}, MakeFormula(descriptor), descriptor.input_value_type,
          std::vector<std::string>{}, "procfs_counter::" + target->Name() + "::" + descriptor.metric_id_suffix);

      const auto status = metric_manager->RegisterCounter(std::move(counter_config), {target});
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
    }
  }

  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
