// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef METRIC_BUILDER_HPP_
#define METRIC_BUILDER_HPP_

#include <expected>
#include <memory>
#include <vector>

#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"

namespace astl {

/** @brief Builds a metric manager from the given set of targets and the configuration
 */
[[nodiscard]] auto BuildMetricManager(const std::vector<std::unique_ptr<ITarget>>& targets,
                                      const AstlConfiguration&                     configuration,
                                      std::optional<std::filesystem::path>         cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code>;

}  // namespace astl

#endif  // COLLECTOR_BUILDER_HPP_
