// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/metric_manager_cache_loader.hpp"

#include <fstream>

#include "astl_logger.hpp"
#include "metric/metric_manager.hpp"

namespace astl {

auto LoadMetricManagerFromCache(const std::vector<std::unique_ptr<ITarget>>& targets,
                                const std::filesystem::path&                 cache_dir_path)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  ASTL_LOG_DEBUG("Loading MetricManager from cache at {}", cache_dir_path.string());
  const std::filesystem::path metric_manager_file_path = cache_dir_path / kMetricManagerFileName;

  if (!std::filesystem::is_directory(cache_dir_path)) {
    ASTL_LOG_ERROR("Invalid ASTL cache directory: {}", cache_dir_path.string());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::ifstream metric_file(metric_manager_file_path, std::ios::binary | std::ios::in);
  if (!metric_file) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto metric_manager = ProtobufSerDes::Deserialize<std::unique_ptr<IMetricManager>>(metric_file, targets);
  if (!metric_manager.has_value()) {
    return std::unexpected(metric_manager.error());
  }

  return metric_manager;
}

}  // namespace astl
