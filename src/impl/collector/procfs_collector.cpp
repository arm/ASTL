// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/procfs_collector.hpp"

#include <vector>

#include "astl_logger.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "common/procfs_utils.hpp"
#include "operation/procfs_read_operation.hpp"

namespace astl {

ProcfsCollector::ProcfsCollector(FileInterface procfs_file_interface)
    : _procfs_file_interface(std::move(procfs_file_interface)) {}

auto ProcfsCollector::GetCapabilities() const -> CollectorCapability { return _collector_capability; }

auto ProcfsCollector::SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void {
  std::scoped_lock lock{_collection_mutex};
  _sample_sink = raw_sample_sink;
}

auto ProcfsCollector::ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::CONFIGURED &&
      _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  _previous_cpu_snapshots.clear();
  _configuration    = std::move(configuration);
  _collection_state = CollectionState::CONFIGURED;
  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

auto ProcfsCollector::ClearCollectionState() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  StopIntervalSampling();
  _previous_cpu_snapshots.clear();
  _configuration.reset();
  _collection_state = CollectionState::UNCONFIGURED;
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCollector::StartCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  if ((_collection_state != CollectionState::CONFIGURED && _collection_state != CollectionState::STOPPED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  _previous_cpu_snapshots.clear();

  auto result = ExecuteStartModeOperations();
  if (result == ASTL_STATUS_SUCCESS) {
    _collection_state = CollectionState::STARTED;
  }
  return result;
}

auto ProcfsCollector::PauseCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_periodic_sampler) {
    _periodic_sampler->Pause();
  } else {
    ASTL_LOG_WARNING("ProcfsCollector: PauseCollection called without an active periodic sampler");
  }
  if (_collection_state == CollectionState::STARTED) {
    _collection_state = CollectionState::PAUSED;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCollector::ResumeCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_periodic_sampler) {
    _periodic_sampler->Resume();
  } else {
    ASTL_LOG_WARNING("ProcfsCollector: ResumeCollection called without an active periodic sampler");
  }
  if (_collection_state == CollectionState::PAUSED) {
    _collection_state = CollectionState::STARTED;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCollector::StopCollection() -> astl_status_code {
  StopIntervalSampling();

  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STOPPED) {
    return ASTL_STATUS_COLLECTION_ALREADY_STOPPED;
  }
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  const auto result = ExecuteStopModeOperations();
  if (result == ASTL_STATUS_SUCCESS) {
    _collection_state = CollectionState::STOPPED;
    _previous_cpu_snapshots.clear();
  }
  return result;
}

auto ProcfsCollector::ReadImmediate() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::CONFIGURED &&
       _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

auto ProcfsCollector::GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock lock{_collection_mutex};
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("ProcfsCollector::GetNativeClockSnapshot called without configuration; returning empty map");
    return {};
  }

  const uint64_t native_now = static_cast<uint64_t>(
      std::chrono::time_point_cast<SampleMicroseconds>(std::chrono::steady_clock::now()).time_since_epoch().count());
  const auto raw_now = ClockMonotonicRaw::now();

  ClockCorrelationMap result;
  for (const auto& operation_ptr : _configuration->Operations().operationsOnSample) {
    result[operation_ptr->GetId()] =
        OperationClockCorrelation{raw_now, native_now, MakeTickRatio<SampleMicroseconds>()};
  }
  return result;
}

auto ProcfsCollector::ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code {
  auto prepared_or_error = PrepareOperations(operations);
  if (!prepared_or_error.has_value()) {
    return prepared_or_error.error();
  }

  std::vector<RawSampledData> collected_samples;
  collected_samples.reserve(operations.size());
  const auto native_tick = static_cast<uint64_t>(
      std::chrono::time_point_cast<SampleMicroseconds>(std::chrono::steady_clock::now()).time_since_epoch().count());

  for (const auto* operation : prepared_or_error->operations) {
    auto value_or_error = ReadOperationSample(*operation, prepared_or_error->cpu_snapshots);
    if (!value_or_error.has_value()) {
      ASTL_LOG_ERROR("ProcfsCollector: failed to read procfs field");
      return value_or_error.error();
    }
    if (!value_or_error->has_value()) {
      continue;
    }

    collected_samples.emplace_back(operation->GetId(), std::move(**value_or_error), native_tick);
  }

  if (!collected_samples.empty() && _sample_sink != nullptr) {
    return _sample_sink->SinkRawSamples(_configuration->Target(), collected_samples);
  }
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCollector::ExecuteStartModeOperations() -> astl_status_code {
  auto status = ExecuteCollectionOperations(_configuration->Operations().operationsAtStart);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  switch (_configuration->CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      status = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      status = StartIntervalSampling();
      break;
    default:
      status = ASTL_STATUS_BAD_CONFIGURATION;
      break;
  }

  return status;
}

auto ProcfsCollector::ExecuteStopModeOperations() -> astl_status_code {
  astl_status_code status = ASTL_STATUS_SUCCESS;
  switch (_configuration->CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      status = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      break;
    default:
      status = ASTL_STATUS_BAD_CONFIGURATION;
      break;
  }

  if (status == ASTL_STATUS_SUCCESS) {
    status = ExecuteCollectionOperations(_configuration->Operations().operationsAtStop);
  }
  return status;
}

auto ProcfsCollector::StartIntervalSampling() -> astl_status_code {
  if (_collection_state != CollectionState::CONFIGURED && _collection_state != CollectionState::STOPPED &&
      _collection_state != CollectionState::PAUSED) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_configuration.has_value()) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (_periodic_sampler) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto interval     = std::chrono::milliseconds{_configuration->CollectionParams().sampling_interval};
  _periodic_sampler = std::make_unique<PeriodicSampler>(this, interval);
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCollector::StopIntervalSampling() -> void { _periodic_sampler = nullptr; }

}  // namespace astl
