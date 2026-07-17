// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_METRIC_AVAILABILITY_HPP_
#define SCMI_METRIC_AVAILABILITY_HPP_

#include "common/metric_config.hpp"
#include "config/astl_configuration.hpp"

namespace astl {

auto FilterUnavailableScmiMetricConfigs(const AstlConfiguration& configuration, MetricConfigOnTargets& configs) -> void;

}  // namespace astl

#endif  // SCMI_METRIC_AVAILABILITY_HPP_
