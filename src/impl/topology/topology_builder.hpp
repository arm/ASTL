// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TOPOLOGY_BUILDER_HPP_
#define TOPOLOGY_BUILDER_HPP_

#include <expected>
#include <memory>

#include "astl/astl_errors.h"
#include "config/configuration_manager.hpp"
#include "topology/i_topology_manager.hpp"

namespace astl {
/**
 * @brief Initialize a topology manager.  This will run as many topology plugins as possible
 * to discover what is available on the current platform.
 */
[[nodiscard]] auto BuildTopologyManager(const AstlConfiguration&             configuration,
                                        std::optional<std::filesystem::path> cache_dir_path)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code>;
}  // namespace astl

#endif  // TOPOLOGY_BUILDER_
