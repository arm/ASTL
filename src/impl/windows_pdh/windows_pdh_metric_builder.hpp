// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WINDOWS_PDH_METRIC_BUILDER_HPP_
#define WINDOWS_PDH_METRIC_BUILDER_HPP_
#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

namespace astl {
class WindowsPdhSystemTarget : public Target {
 public:
  explicit WindowsPdhSystemTarget(std::vector<std::string> processors)
      : Target("sysutil", "Windows PDH system telemetry", CollectorType::WINDOWS_PDH),
        _processors(std::move(processors)) {}
  auto Processors() const -> const std::vector<std::string>& { return _processors; }

 private:
  std::vector<std::string> _processors;
};
auto RegisterWindowsPdhMetrics(const AstlConfiguration&                                              configuration,
                               const std::unordered_map<CollectorType, std::vector<const ITarget*>>& targets,
                               IMetricManager* manager) -> astl_status_code;
}  // namespace astl
#endif
