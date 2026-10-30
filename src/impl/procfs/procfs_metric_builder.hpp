// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_METRIC_BUILDER_HPP_
#define PROCFS_METRIC_BUILDER_HPP_

#include <unordered_map>
#include <vector>

#include "common/capabilities.hpp"
#include "config/astl_configuration.hpp"

namespace astl {

struct ITarget;
struct IMetricManager;

auto RegisterProcfsMetrics(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code;

auto RegisterProcfsCounters(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code;

}  // namespace astl

#endif  // PROCFS_METRIC_BUILDER_HPP_
