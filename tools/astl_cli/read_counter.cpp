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

#include "read_counter.hpp"

#include <iostream>
#include <ranges>
#include <string>
#include <thread>

#include "app_config.hpp"
#include "astl/astl_telemetry.h"
#include "astl_counters.hpp"
#include "utils.hpp"

static void PrintCommandConfig(const AppConfig& cfg) {
  if (!cfg.verbose) {
    return;
  }
  std::cout << "[read-counter]\n";
  std::cout << "  counter-name  : " << cfg.read_counter.counter_name << "\n";
}

auto SelectCounter(const std::vector<Counter>& all_counters, const AppConfig& cfg)
    -> std::expected<Counter, astl_status_code> {
  std::string desired_counter_name = cfg.read_counter.counter_name;
  std::ranges::transform(desired_counter_name, desired_counter_name.begin(),
                         [](unsigned char letter) { return std::tolower(letter); });

  for (const auto& counter : all_counters) {
    std::string this_counter_name = counter.properties._name;
    std::ranges::transform(this_counter_name, this_counter_name.begin(),
                           [](unsigned char letter) { return std::tolower(letter); });
    // skip counters that don't match the desired name
    if (this_counter_name != desired_counter_name) {
      if (cfg.verbose) {
        std::cout << "Skipping counter: " << counter.properties._name << "\n";
      }
      continue;
    }
    return counter;
  }
  return std::unexpected{ASTL_STATUS_NO_COUNTERS_FOUND};
}

static auto RetrieveCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle)
    -> std::vector<astl_counter_sample_t> {
  uint32_t sample_count = 0;
  auto     result       = astlGetCounterSampleCountOnTarget(target_handle, counter_handle, &sample_count);
  if (result != ASTL_STATUS_SUCCESS) {
    std::cerr << "Error getting counter sample count on target: " << astlStatusString(result) << "\n";
    return {};
  }
  if (sample_count == 0) {
    return {};
  }
  std::vector<astl_counter_sample_t> samples(sample_count);
  std::ranges::for_each(samples, [](astl_counter_sample_t& sample) { sample._size = sizeof(astl_counter_sample_t); });
  result = astlGetCounterSamplesOnTarget(target_handle, counter_handle, samples.data(), &sample_count);
  if (result != ASTL_STATUS_SUCCESS) {
    std::cerr << "Error getting counter samples on target: " << astlStatusString(result) << "\n";
    return {};
  }
  return samples;
}

auto PrintCounterSamples(const AppConfig& cfg, Counter const& selected_counter) -> int {
  // enumerate all counters, and get all samples on all targets for each counter.
  // print all samples for each counter/target pair.
  for (const auto& [target_handle, target_name] : selected_counter.target_handles_and_names) {
    auto samples = RetrieveCounterSamplesOnTarget(target_handle, selected_counter.handle);
    if (cfg.verbose) {
      // print samples
      std::cout << "Collected " << samples.size() << " samples for counter " << selected_counter.properties._name
                << "\n";
    }
    std::ranges::for_each(samples, [](const astl_counter_sample_t& sample) {
      std::cout << "  timestamp: " << sample._timestamp << " ms, value: " << sample._value.ui64 << "\n";
    });
  }
  return 0;
}

int ReadCounter(const AppConfig& cfg) {
  PrintCommandConfig(cfg);
  auto counters_or_error = DetectCounters(cfg);
  if (!counters_or_error) {
    std::cerr << "Error detecting counters: " << astlStatusString(counters_or_error.error()) << "\n";
    return -1;
  }
  auto selected_counter_or_error = SelectCounter(*counters_or_error, cfg);
  if (!selected_counter_or_error) {
    std::cerr << "Error selecting counter: " << astlStatusString(selected_counter_or_error.error()) << "\n";
    return -1;
  }
  auto selected_counter = *selected_counter_or_error;

  astl_collection_parameters_t collection_params = {._size              = sizeof(astl_collection_parameters_t),
                                                    ._sampling_interval = static_cast<uint32_t>(0),
                                                    ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
                                                    ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD};

  auto result = ASTL_STATUS_SUCCESS;
  // configure
  for (const auto& [target_handle, target_name] : selected_counter.target_handles_and_names) {
    if (cfg.verbose) {
      std::cout << "Configuring counter " << selected_counter.properties._name << " on target " << target_name << "\n";
    }
    std::vector<astl_counter_handle_t> counters = {selected_counter.handle};
    result = astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, counters.data(),
                                                    static_cast<uint32_t>(counters.size()));
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error configuring counter collection: " << astlStatusString(result) << "\n";
      return -1;
    }
  }
  for (const auto& [target_handle, target_name] : selected_counter.target_handles_and_names) {
    result = astlStartCollectionOnTarget(target_handle);
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error starting counter collection on target: " << astlStatusString(result) << "\n";
      return -1;
    }
  }
  // read one sample
  for (const auto& [target_handle, target_name] : selected_counter.target_handles_and_names) {
    result = astlReadImmediateOnTarget(target_handle);
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error reading counter on target: " << astlStatusString(result) << "\n";
      return -1;
    }
  }
  // stop collection
  for (const auto& [target_handle, target_name] : selected_counter.target_handles_and_names) {
    result = astlStopCollectionOnTarget(target_handle);
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error stopping counter collection on target: " << astlStatusString(result) << "\n";
      return -1;
    }
  }
  return PrintCounterSamples(cfg, selected_counter);
}
