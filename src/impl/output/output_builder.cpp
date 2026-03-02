// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include "output/output_manager.hpp"

namespace astl {

/**
 * @brief Builds outputs for the given targets based on the provided configuration.
 *
 * @param targets The list of targets for which outputs are to be built.
 * @param configuration The configuration containing parameters for output creation.
 * @return An initialized IOutputManager associating each target with its corresponding outputs, or an error code.
 */
auto BuildOutputManager() -> std::expected<std::unique_ptr<IOutputManager>, astl_status_code> {
  return std::make_unique<OutputManager>();
}

}  // namespace astl
