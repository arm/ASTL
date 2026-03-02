// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef LIBSENSORS_METRIC_BUILDER_HPP_
#define LIBSENSORS_METRIC_BUILDER_HPP_

#include <unordered_map>
#include <vector>

#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Scan the collector_type_to_targets_map for Libsensors targets.
 *        Use the given configuration to create metrics and register them in the metric_manager.
 */
auto RegisterLibsensorsMetrics(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    IMetricManager*                                                       metric_manager) -> astl_status_code;

}  // namespace astl
#endif  // LIBSENSORS_METRIC_BUILDER_HPP_
