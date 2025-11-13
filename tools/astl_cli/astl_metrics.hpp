/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#ifndef ASTL_METRICS_HPP
#define ASTL_METRICS_HPP

#include <expected>
#include <ranges>

#include "app_config.hpp"
#include "astl/astl_telemetry.h"

inline auto InitializeASTL(const char* config_file_path) -> astl_status_code {
  return astl::SetEnvVar("ASTL_CONFIG_JSON_PATH", config_file_path);
}

inline auto GetTargets() -> std::expected<std::vector<astl_target_properties_t>, astl_status_code> {
  uint32_t         target_count = 0;
  astl_status_code status       = astlGetTargetCount(&target_count);
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{status};
  }
  if (target_count == 0) {
    std::cerr << "No targets found\n";
    return std::unexpected{ASTL_STATUS_NO_TARGETS_FOUND};
  }
  std::vector<astl_target_properties_t> target_properties_buffer{target_count};
  target_properties_buffer[0]._size = sizeof(astl_target_properties_t);
  status                            = astlGetTargets(target_properties_buffer.data(), &target_count);
  if (status != ASTL_STATUS_SUCCESS && status != ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED) {
    return std::unexpected{status};
  }
  target_properties_buffer.resize(target_count);
  return target_properties_buffer;
}

inline auto GetMetricsOnTarget(astl_target_properties_t const& target)
    -> std::expected<std::vector<astl_metric_properties_t>, astl_status_code> {
  uint32_t         metric_count = 0;
  astl_status_code status       = astlGetMetricCount(target._handle, &metric_count);
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{status};
  }
  if (metric_count == 0) {
    return std::vector<astl_metric_properties_t>{};  // no metrics on this target
  }
  std::vector<astl_metric_properties_t> metric_properties_buffer{metric_count};
  metric_properties_buffer[0]._size = sizeof(astl_metric_properties_t);
  status                            = astlGetMetrics(target._handle, metric_properties_buffer.data(), &metric_count);
  if (status != ASTL_STATUS_SUCCESS && status != ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED) {
    return std::unexpected{status};
  }
  metric_properties_buffer.resize(metric_count);
  return metric_properties_buffer;
}

struct Metric {
  astl_metric_handle_t                                      handle{nullptr};
  astl_metric_properties_t                                  properties;
  std::vector<std::pair<astl_target_handle_t, std::string>> target_handles_and_names;

  Metric(astl_metric_handle_t handle, astl_metric_properties_t properties,
         std::vector<std::pair<astl_target_handle_t, std::string>> target_handles_and_names)
      : handle{handle},
        properties{std::move(properties)},
        target_handles_and_names{std::move(target_handles_and_names)} {}
};

// Detect all metrics on all targets, returning a list of unique metrics with the targets they belong to
inline auto DetectMetrics(const AppConfig& cfg) -> std::expected<std::vector<Metric>, astl_status_code> {
  auto status = InitializeASTL(cfg.astl_config_file.string().c_str());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{status};
  }
  auto targets_or_error = GetTargets();
  if (!targets_or_error) {
    return std::unexpected{targets_or_error.error()};
  }
  std::vector<Metric> all_metrics;
  for (const auto& target : *targets_or_error) {
    auto metrics_or_error = GetMetricsOnTarget(target);
    if (!metrics_or_error) {
      return std::unexpected{metrics_or_error.error()};
    }
    for (const auto& metric : *metrics_or_error) {
      auto idx = std::ranges::find_if(all_metrics, [&](const Metric& some) { return some.handle == metric._handle; });
      if (idx != all_metrics.end()) {
        // already have this metric, just add the target handle
        idx->target_handles_and_names.emplace_back(target._handle, target._name);
      } else {
        // new metric, add to list. make a new metric entry, copying the properties
        all_metrics.push_back(Metric{metric._handle, metric, {{target._handle, target._name}}});
      }
    }
  }
  return all_metrics;
}

#endif  // ASTL_METRICS_HPP