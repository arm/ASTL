// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef METRIC_MANAGER_CACHE_LOADER_HPP_
#define METRIC_MANAGER_CACHE_LOADER_HPP_

#include <expected>
#include <filesystem>
#include <memory>
#include <vector>

#include "metric/i_metric_manager.hpp"

namespace astl {

[[nodiscard]] auto LoadMetricManagerFromCache(const std::vector<std::unique_ptr<ITarget>>& targets,
                                              const std::filesystem::path&                 cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code>;

}  // namespace astl

#endif  // METRIC_MANAGER_CACHE_LOADER_HPP_
