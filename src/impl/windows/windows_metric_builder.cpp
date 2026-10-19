// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "windows/windows_metric_builder.hpp"

#include "windows/windows_operation_builder.hpp"

namespace astl {
namespace {
struct MetricDefinition {
  std::string        name;
  std::string        description;
  WindowsSystemField field;
  std::string        instance;
};

auto RegisterMetric(const WindowsSystemTarget& target, IMetricManager& manager, const MetricDefinition& definition)
    -> astl_status_code {
  const auto& [name, description, field, instance] = definition;
  const bool cpu                                   = field == WindowsSystemField::CPU_UTILIZATION;
  const bool percent                               = cpu || field == WindowsSystemField::MEMORY_UTILIZATION;
  auto       config                                = std::make_unique<MetricConfig>(
      name, description, percent ? ASTL_UNITS_PERCENT : ASTL_UNITS_BYTES,
      percent ? ASTL_VALUE_FLOAT64 : ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
      CollectorType::WINDOWS, WindowsOperationBuilder{field, instance}, IdentityFormula{}, ASTL_VALUE_UNKNOWN,
      std::vector<std::string>{cpu ? "cpu" : "memory"}, "windows::" + name);
  return manager.RegisterMetric(std::move(config), {&target});
}

auto RegisterMemoryMetrics(const WindowsSystemTarget& target, IMetricManager& manager) -> astl_status_code {
  for (const auto& [name, field] : {
           std::pair{"memory.total",       WindowsSystemField::MEMORY_TOTAL      },
           std::pair{"memory.used",        WindowsSystemField::MEMORY_USED       },
           std::pair{"memory.utilization", WindowsSystemField::MEMORY_UTILIZATION}
  }) {
    const auto status = RegisterMetric(target, manager, {name, "Windows physical " + std::string{name}, field, ""});
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto RegisterCpuMetrics(const WindowsSystemTarget& target, IMetricManager& manager) -> astl_status_code {
  if (!target.Processors().empty()) {
    auto status = RegisterMetric(target, manager,
                                 {"cpu.total.utilization", "Aggregate logical processor busy-time percentage",
                                  WindowsSystemField::CPU_UTILIZATION, "_Total"});
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
    for (size_t index = 0; index < target.Processors().size(); ++index) {
      const auto& instance = target.Processors()[index];
      status               = RegisterMetric(target, manager,
                                            {"cpu." + std::to_string(index) + ".utilization",
                                             "Logical processor busy-time percentage (Windows instance " + instance + ")",
                                             WindowsSystemField::CPU_UTILIZATION, instance});
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}
auto RegisterTargetMetrics(const WindowsSystemTarget& target, IMetricManager& manager) -> astl_status_code {
  const auto status = RegisterMemoryMetrics(target, manager);
  return status == ASTL_STATUS_SUCCESS ? RegisterCpuMetrics(target, manager) : status;
}
}  // namespace

auto RegisterWindowsMetrics(const AstlConfiguration&                                              configuration,
                            const std::unordered_map<CollectorType, std::vector<const ITarget*>>& targets,
                            IMetricManager* manager) -> astl_status_code {
  (void)configuration;
  if (!manager) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto found = targets.find(CollectorType::WINDOWS);
  if (found == targets.end()) {
    return ASTL_STATUS_SUCCESS;
  }
  for (const auto* target : found->second) {
    const auto* windows = dynamic_cast<const WindowsSystemTarget*>(target);
    if (!windows) {
      return ASTL_STATUS_BAD_CONFIGURATION;
    }
    const auto status = RegisterTargetMetrics(*windows, *manager);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}
}  // namespace astl
