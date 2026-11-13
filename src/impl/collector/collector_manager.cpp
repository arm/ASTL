// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector_manager.hpp"

#include <algorithm>

#include "astl/astl.h"
#include "astl_cxx_compat.hpp"
#include "collection_configuration.hpp"
#include "immediate_read_outcome.hpp"
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

auto CollectorManager::ClearConfiguredCollections() -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  if (!_targets_with_active_collection.empty()) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }

  astl_status_code result = ASTL_STATUS_SUCCESS;
  for (auto& [target, collectors] : _collectors) {
    (void)target;
    for (auto& collector : collectors) {
      const auto clear_status = collector->ClearCollectionState();
      if (clear_status != ASTL_STATUS_SUCCESS) {
        result = clear_status;
      }
    }
  }
  return result;
}

auto CollectorManager::ClearConfiguredCollectionOnTarget(const ITarget* target) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_targets_with_active_collection.contains(target)) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  const auto collectors = _collectors.find(target);
  if (collectors == _collectors.end() || collectors->second.empty()) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  astl_status_code result = ASTL_STATUS_SUCCESS;
  for (auto& collector : collectors->second) {
    const auto status = collector->ClearCollectionState();
    if (status != ASTL_STATUS_SUCCESS) {
      result = status;
    }
  }
  return result;
}

auto CollectorManager::StartOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = FindFirstCollector(target);
  if (!selected_collector) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  const auto status = selected_collector->StartCollection();
  if (status == ASTL_STATUS_SUCCESS) {
    std::lock_guard<std::mutex> lock(_mutex);
    _targets_with_active_collection.insert(target);
  }
  return status;
}

auto CollectorManager::GetNativeClockSnapshot(const ITarget* target)
    -> astl::compat::expected<ClockCorrelationMap, astl_status_code> {
  ICollector* selected_collector = FindFirstCollector(target);
  if (!selected_collector) {
    ASTL_LOG_WARNING("CollectorManager::GetNativeClockSnapshot: no collector found for target");
    return astl::compat::unexpected{ASTL_STATUS_INVALID_TARGET_HANDLE};
  }
  return selected_collector->GetNativeClockSnapshot();
}

auto CollectorManager::PauseOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = FindFirstCollector(target);
  if (!selected_collector) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return selected_collector->PauseCollection();
}

auto CollectorManager::ResumeOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = FindFirstCollector(target);
  if (!selected_collector) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return selected_collector->ResumeCollection();
}

auto CollectorManager::ReadImmediateOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = FindFirstCollector(target);
  if (!selected_collector) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  detail::ImmediateReadOutcome outcome{this, target};
  const auto                   status = selected_collector->ReadImmediate();
  return status == ASTL_STATUS_SUCCESS ? outcome.Status() : status;
}

auto CollectorManager::StopOnTarget(const ITarget* target) -> astl_status_code {
  ICollector* selected_collector = FindFirstCollector(target);
  if (!selected_collector) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
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
auto CollectorManager::SinkRawSampleOutcome(const ITarget* target, OperationId operation_id,
                                            astl::EmptyResultReason reason) -> void {
  std::lock_guard<std::mutex> lock(_mutex);
  if (auto* outcome = detail::ImmediateReadOutcome::For(this, target)) {
    if (reason == EmptyResultReason::SAMPLE_DROPPED ||
        (reason == EmptyResultReason::NO_NEW_SAMPLE && outcome->reason == EmptyResultReason::UNKNOWN)) {
      outcome->reason = reason;
    }
  }
  for (auto* sink : _registered_raw_sample_sinks) {
    sink->SinkRawSampleOutcome(target, operation_id, reason);
  }
}

auto CollectorManager::SinkRawSamples(const ITarget* target, std::span<RawSampledData> raw_samples)
    -> astl_status_code {
  // NOTE:
  // Sinks are invoked while holding _mutex.
  // RegisterRawSampleSink()/UnregisterRawSampleSink() must NOT be called from
  // within SinkRawSamples callbacks, directly or indirectly, or this can deadlock.
  std::lock_guard<std::mutex> lock(_mutex);
  if (auto* outcome = detail::ImmediateReadOutcome::For(this, target)) {
    outcome->has_samples |=
        std::ranges::any_of(raw_samples, [](const auto& sample) { return !sample.IsPauseResumeMarker(); });
  }
  astl_status_code result = ASTL_STATUS_SUCCESS;
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

auto CollectorManager::FindFirstCollector(const ITarget* target) -> ICollector* {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto                  collector = _collectors.find(target);
  return collector != _collectors.end() && !collector->second.empty() ? collector->second.front().get() : nullptr;
}

/**
 * @brief Given a target and required capabilities, choose a suitable collector.
 *
 * Requires: caller holds `_mutex`.
 */
auto CollectorManager::SelectCollectorLocked(const ITarget* target, CollectorCapability const& requirements)
    -> astl::compat::expected<ICollector*, astl_status_code> {
  // find a set of collectors associated with the given target
  const auto& potential_collectors = _collectors.find(target);
  if (potential_collectors == _collectors.end()) {
    return astl::compat::unexpected(ASTL_STATUS_NO_TARGET_FOUND);
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
    return astl::compat::unexpected(ASTL_STATUS_INVALID_COLLECTION_MODE);
  }
  return first_matching_collector->get();
}

}  // namespace astl
