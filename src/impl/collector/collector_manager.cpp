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

#include "collector_manager.hpp"

#include <algorithm>
#include <expected>

#include "astl/astl.h"
#include "collection_configuration.hpp"

namespace astl {

CollectorManager::CollectorManager(std::unordered_map<ITarget*, std::vector<std::unique_ptr<ICollector>>>&& collectors)
    : _collectors{std::move(collectors)} {
  // tell each collector to send their samples to CollectorManager. Each Collector has only one sample-sink,
  // but collector manager can support multiple sinks.
  std::ranges::for_each(_collectors, [this](auto& target_collectors_iter) {
    std::ranges::for_each(target_collectors_iter.second, [this](auto& collector) { collector->SetSampleSink(this); });
  });
}
////////////////////////////////////////////////////////////////////////////////
// ICollectorManager implementation
////////////////////////////////////////////////////////////////////////////////

std::unordered_map<ITarget*, std::vector<CollectorCapability>> CollectorManager::ReportCollectionCapabilities() const {
  std::unordered_map<ITarget*, std::vector<CollectorCapability>> capabilities;
  for (const auto& [target, collectors] : _collectors) {
    for (const auto& collector : collectors) {
      capabilities[target].push_back(collector->GetCapabilities());
    }
  }
  return capabilities;
}

astl_status_code CollectorManager::RegisterSampleSink(ISampleSink* sink) {
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  _registered_sample_sinks.insert(sink);
  return ASTL_STATUS_SUCCESS;
}

astl_status_code CollectorManager::UnregisterSampleSink(ISampleSink* sink) {
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto num_removed = _registered_sample_sinks.erase(sink);
  if (num_removed == 0) {
    return ASTL_STATUS_INTERNAL_ERROR;  // sink was not registered
  }
  return ASTL_STATUS_SUCCESS;
}

astl_status_code CollectorManager::ConfigureCollectionOnTarget(ITarget*                            target,
                                                               astl_collection_parameters_t const& collection_params,
                                                               CollectionOperations&&              operations) {
  auto collector = SelectCollector(target, operations.requirements);
  if (!collector) {
    return collector.error();
  }
  CollectionConfiguration configuration_instance(target, std::move(operations), collection_params);
  return collector.value()->ConfigureCollection(std::move(configuration_instance));
}

astl_status_code CollectorManager::StartOnTarget(ITarget* target) {
  if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
    // if we have a collector for this target, start it
    return collector->second.front()->StartCollection();
  }
  return ASTL_STATUS_INVALID_TARGET_HANDLE;
}

astl_status_code CollectorManager::PauseOnTarget(ITarget* target) {
  if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
    return collector->second.front()->PauseCollection();
  }
  return ASTL_STATUS_INVALID_TARGET_HANDLE;
}

astl_status_code CollectorManager::ResumeOnTarget(ITarget* target) {
  if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
    return collector->second.front()->ResumeCollection();
  }
  return ASTL_STATUS_INVALID_TARGET_HANDLE;
}

astl_status_code CollectorManager::ReadImmediateOnTarget(ITarget* target) {
  if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
    // if we have a collector for this target, sample from it
    return collector->second.front()->ReadImmediate();
  }
  return ASTL_STATUS_INVALID_TARGET_HANDLE;
}

astl_status_code CollectorManager::StopOnTarget(ITarget* target) {
  if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
    // if we have a collector for this target, start it
    return collector->second.front()->StopCollection();
  }
  return ASTL_STATUS_INVALID_TARGET_HANDLE;
}

////////////////////////////////////////////////////////////////////////////////
// ISampleSink implementation
////////////////////////////////////////////////////////////////////////////////
astl_status_code CollectorManager::SinkSamples(ITarget* target, std::span<SampledData> samples) {
  astl_status_code result = ASTL_STATUS_SUCCESS;
  for (const auto& sink : _registered_sample_sinks) {
    auto sink_result = sink->SinkSamples(target, samples);
    // will return the most recent failure, but continues trying to send samples to all sinks
    if (sink_result != ASTL_STATUS_SUCCESS) {
      result = sink_result;
    }
  }
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// private helpers
////////////////////////////////////////////////////////////////////////////////

// given a target and a set of desired capabilities, choose a suiteable collector
std::expected<ICollector*, astl_status_code> CollectorManager::SelectCollector(
    ITarget* target, CollectorCapability const& requirements) {
  // find a set of collectors associated with the given target
  const auto& potential_collectors = _collectors.find(target);
  if (potential_collectors == _collectors.end()) {
    return std::unexpected(ASTL_STATUS_NO_TARGETS_FOUND);
  }

  // choose a collector that meets the requirements
  auto first_matching_collector =
      std::ranges::find_if(potential_collectors->second, [&requirements](const auto& collector) {
        return collector->GetCapabilities().collector_type == requirements.collector_type;
      });

  if (first_matching_collector == potential_collectors->second.end()) {
    return std::unexpected(ASTL_STATUS_INVALID_COLLECTION_MODE);
  }
  return first_matching_collector->get();
}

}  // namespace astl
