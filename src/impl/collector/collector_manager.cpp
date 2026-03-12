// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector_manager.hpp"

#include <algorithm>
#include <expected>

#include "astl/astl.h"
#include "collection_configuration.hpp"
namespace astl {

CollectorManager::CollectorManager(
    std::unordered_map<const ITarget*, std::vector<std::unique_ptr<ICollector>>>&& collectors)
    : _collectors{std::move(collectors)} {
  // tell each collector to send their samples to CollectorManager. Each Collector has only one sample-sink,
  // but collector manager can support multiple sinks.
  for (auto& target_collectors : _collectors) {
    for (auto& cur_collector : target_collectors.second) {
      cur_collector->SetRawSampleSink(this);
    }
  }
}
////////////////////////////////////////////////////////////////////////////////
// ICollectorManager implementation
////////////////////////////////////////////////////////////////////////////////

auto CollectorManager::ReportCollectionCapabilities() const
    -> std::unordered_map<const ITarget*, std::vector<CollectorCapability>> {
  std::lock_guard<std::mutex>                                          lock(_mutex);
  std::unordered_map<const ITarget*, std::vector<CollectorCapability>> capabilities;
  for (const auto& [target, collectors] : _collectors) {
    for (const auto& collector : collectors) {
      capabilities[target].push_back(collector->GetCapabilities());
    }
  }
  return capabilities;
}

auto CollectorManager::RegisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code {
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  _registered_raw_sample_sinks.insert(sink);
  return ASTL_STATUS_SUCCESS;
}

auto CollectorManager::UnregisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code {
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  auto                        num_removed = _registered_raw_sample_sinks.erase(sink);
  if (num_removed == 0) {
    return ASTL_STATUS_INTERNAL_ERROR;  // sink was not registered
  }
  return ASTL_STATUS_SUCCESS;
}

auto CollectorManager::ConfigureCollectionOnTarget(const ITarget*                  target,
                                                   astl_collection_params_t const& collection_params,
                                                   CollectionOperations&&          operations) -> astl_status_code {
  ICollector* selected_collector = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    auto                        collector = SelectCollectorLocked(target, operations.requirements);
    if (!collector) {
      return collector.error();
    }
    selected_collector = collector.value();
  }
  CollectionConfiguration configuration_instance(target, std::move(operations), collection_params);
  return selected_collector->ConfigureCollection(std::move(configuration_instance));
}

auto CollectorManager::StartOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
      selected_collector = collector->second.front().get();
    } else {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
  }
  const auto status = selected_collector->StartCollection();
  if (status == ASTL_STATUS_SUCCESS) {
    std::lock_guard<std::mutex> lock(_mutex);
    _targets_with_active_collection.insert(target);
  }
  return status;
}

auto CollectorManager::PauseOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
      selected_collector = collector->second.front().get();
    } else {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
  }
  return selected_collector->PauseCollection();
}

auto CollectorManager::ResumeOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
      selected_collector = collector->second.front().get();
    } else {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
  }
  return selected_collector->ResumeCollection();
}

auto CollectorManager::ReadImmediateOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
      // if we have a collector for this target, sample from it
      selected_collector = collector->second.front().get();
    } else {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
  }
  return selected_collector->ReadImmediate();
}

auto CollectorManager::StopOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto collector = _collectors.find(target); collector != _collectors.end() && !collector->second.empty()) {
      selected_collector = collector->second.front().get();
    } else {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
  }
  const auto status = selected_collector->StopCollection();
  if (status == ASTL_STATUS_SUCCESS) {
    std::lock_guard<std::mutex> lock(_mutex);
    _targets_with_active_collection.erase(target);
  }
  return status;
}

auto CollectorManager::IsAnyTargetBeingCollected() const -> bool {
  std::lock_guard<std::mutex> lock(_mutex);
  return !_targets_with_active_collection.empty();
}

////////////////////////////////////////////////////////////////////////////////
// IRawSampleSink implementation
////////////////////////////////////////////////////////////////////////////////
auto CollectorManager::SinkRawSamples(const ITarget* target, std::span<RawSampledData> raw_samples)
    -> astl_status_code {
  // NOTE:
  // Sinks are invoked while holding _mutex.
  // RegisterRawSampleSink()/UnregisterRawSampleSink() must NOT be called from
  // within SinkRawSamples callbacks, directly or indirectly, or this can deadlock.
  std::lock_guard<std::mutex> lock(_mutex);
  astl_status_code            result = ASTL_STATUS_SUCCESS;
  for (auto* sink : _registered_raw_sample_sinks) {
    auto sink_result = sink->SinkRawSamples(target, raw_samples);
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

/**
 * @brief Given a target and required capabilities, choose a suitable collector.
 *
 * Requires: caller holds `_mutex`.
 */
auto CollectorManager::SelectCollectorLocked(const ITarget* target, CollectorCapability const& requirements)
    -> std::expected<ICollector*, astl_status_code> {
  // find a set of collectors associated with the given target
  const auto& potential_collectors = _collectors.find(target);
  if (potential_collectors == _collectors.end()) {
    return std::unexpected(ASTL_STATUS_NO_TARGETS_FOUND);
  }

  // choose a collector that meets the requirements
  auto first_matching_collector =
      std::ranges::find_if(potential_collectors->second, [&requirements](const auto& collector) {
        const auto capabilities            = collector->GetCapabilities();
        const auto collector_type          = capabilities.collector_type;
        const auto required_collector_type = requirements.collector_type;
        return collector_type == required_collector_type;
      });

  if (first_matching_collector == potential_collectors->second.end()) {
    return std::unexpected(ASTL_STATUS_INVALID_COLLECTION_MODE);
  }
  return first_matching_collector->get();
}

}  // namespace astl
