// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WINDOWS_METRIC_BUILDER_HPP_
#define WINDOWS_METRIC_BUILDER_HPP_
#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

namespace astl {
class WindowsSystemTarget : public Target {
 public:
  explicit WindowsSystemTarget(std::vector<std::string> processors)
      : Target("sysutil", "Windows system telemetry", CollectorType::WINDOWS), _processors(std::move(processors)) {}
  auto Processors() const -> const std::vector<std::string>& { return _processors; }

 private:
  std::vector<std::string> _processors;
};
auto RegisterWindowsMetrics(const AstlConfiguration&                                              configuration,
                            const std::unordered_map<CollectorType, std::vector<const ITarget*>>& targets,
                            IMetricManager* manager) -> astl_status_code;
}  // namespace astl
#endif
