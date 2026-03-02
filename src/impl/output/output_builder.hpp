// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file output_builder.hpp
 * @brief Helper for constructing an `IOutputManager` based on runtime configuration.
 */
#ifndef OUTPUT_BUILDER_HPP_
#define OUTPUT_BUILDER_HPP_

#include <memory>

#include "output/i_output_manager.hpp"

namespace astl {

/**
 * @brief Construct and initialize an output manager instance.
 *
 * Reads global / per-target output configuration (if available) and creates the appropriate set of
 * output sinks. On success ownership of the manager is returned to the caller.
 *
 * Error Handling:
 *  - Returns `std::unexpected(astl_status_code)` if configuration parsing, allocation, or output
 *    instantiation fails.
 */
[[nodiscard]] auto BuildOutputManager() -> std::expected<std::unique_ptr<IOutputManager>, astl_status_code>;

}  // namespace astl

#endif  // COLLECTOR_BUILDER_HPP_
