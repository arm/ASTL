// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTOR_BUILDER_HPP_
#define COLLECTOR_BUILDER_HPP_

#include <memory>
#include <vector>

#include "collector/i_collector_manager.hpp"
#include "config/astl_configuration.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Builds collectors for the given targets based on the provided configuration.
 *
 * @param targets The list of targets for which collectors are to be built.
 * @param configuration The configuration containing parameters for collector creation.
 * @return An initialized ICollectorManager associating each target with its corresponding collectors, or an error code.
 *         Note the RegisterRawSampleSink() function will still need to be called on the returned collector manager
 */
[[nodiscard]] auto BuildCollectorManager(const std::vector<std::unique_ptr<ITarget>>& targets,
                                         const AstlConfiguration&                     configuration)
    -> std::expected<std::unique_ptr<ICollectorManager>, astl_status_code>;

}  // namespace astl

#endif  // COLLECTOR_BUILDER_HPP_
