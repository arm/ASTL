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

#ifndef ASTL_COUNTERS_HPP
#define ASTL_COUNTERS_HPP

#include <expected>
#include <ranges>

#include "app_config.hpp"
#include "astl/astl_telemetry.h"

inline auto InitializeASTL(const char* config_file_path) -> astl_status_code {
  return astl::SetEnvVar("ASTL_CONFIG_JSON_PATH", config_file_path);
}

inline auto GetTargets(const AppConfig& cfg) -> std::expected<std::vector<astl_target_properties_t>, astl_status_code> {
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
  if (cfg.verbose) {
    std::cout << "Detected " << target_count << " targets\n";
    for (const auto& target : target_properties_buffer) {
      std::cout << "  Target: " << target._name << " (" << target._description << ") at " << target._handle << "\n";
    }
  }
  return target_properties_buffer;
}

inline auto GetCountersOnTarget(astl_target_properties_t const& target)
    -> std::expected<std::vector<astl_counter_properties_t>, astl_status_code> {
  uint32_t         counter_count = 0;
  astl_status_code status        = astlGetCounterCount(target._handle, &counter_count);
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{status};
  }
  if (counter_count == 0) {
    return std::vector<astl_counter_properties_t>{};  // no counters on this target
  }
  std::vector<astl_counter_properties_t> counter_properties_buffer{counter_count};
  counter_properties_buffer[0]._size = sizeof(astl_counter_properties_t);
  status = astlGetCounters(target._handle, counter_properties_buffer.data(), &counter_count);
  if (status != ASTL_STATUS_SUCCESS && status != ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED) {
    return std::unexpected{status};
  }
  counter_properties_buffer.resize(counter_count);
  return counter_properties_buffer;
}

struct Counter {
  astl_counter_handle_t                                     handle{nullptr};
  astl_counter_properties_t                                 properties;
  std::vector<std::pair<astl_target_handle_t, std::string>> target_handles_and_names;

  Counter(astl_counter_handle_t handle, astl_counter_properties_t properties,
          std::vector<std::pair<astl_target_handle_t, std::string>> target_handles_and_names)
      : handle{handle},
        properties{std::move(properties)},
        target_handles_and_names{std::move(target_handles_and_names)} {}
};

// Detect all counters on all targets, returning a list of unique counters with the targets they belong to
inline auto DetectCounters(const AppConfig& cfg) -> std::expected<std::vector<Counter>, astl_status_code> {
  auto status = InitializeASTL(cfg.astl_config_file.string().c_str());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{status};
  }
  auto targets_or_error = GetTargets(cfg);
  if (!targets_or_error) {
    return std::unexpected{targets_or_error.error()};
  }
  std::vector<Counter> all_counters;
  for (const auto& target : *targets_or_error) {
    auto counters_or_error = GetCountersOnTarget(target);
    if (!counters_or_error) {
      return std::unexpected{counters_or_error.error()};
    }
    for (const auto& counter : *counters_or_error) {
      auto idx =
          std::ranges::find_if(all_counters, [&](const Counter& some) { return some.handle == counter._handle; });
      if (idx != all_counters.end()) {
        // already have this counter, just add the target handle
        idx->target_handles_and_names.emplace_back(target._handle, target._name);
      } else {
        // new counter, add to list. make a new counter entry, copying the properties
        all_counters.push_back(Counter{counter._handle, counter, {{target._handle, target._name}}});
      }
    }
  }
  return all_counters;
}

#endif  // ASTL_COUNTERS_HPP