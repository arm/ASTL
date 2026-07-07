// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "libsensors/libsensors_collector.hpp"

#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "astl_logger.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/periodic_sampler.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "libsensors/libsensors_read_operation.hpp"
#include "operation/operation.hpp"

namespace astl {

LibsensorsCollector::LibsensorsCollector(std::shared_ptr<SensorsApi> sensors_api)
    : _sensors_api(std::move(sensors_api)) {}

/*
 * @brief Get the capabilities of this collector, including the collector type.
 */
auto LibsensorsCollector::GetCapabilities() const -> CollectorCapability { return _collector_capability; };

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any ISampleSink.
 */
auto LibsensorsCollector::SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void {
  std::scoped_lock lock{_collection_mutex};
  _sample_sink = raw_sample_sink;
};

/*
 * @brief Configure the collector to collect data, but don't start sampling it yet.
 *
 * @param configuration The configuration to apply to this collector, including the set of operations to run,
 *        the interval to sample at.
 */
auto LibsensorsCollector::ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::CONFIGURED &&
      _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot reconfigure while already started
  }
  _configuration    = std::move(configuration);
  _collection_state = CollectionState::CONFIGURED;
  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

auto LibsensorsCollector::ClearCollectionState() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  StopIntervalSampling();
  _configuration.reset();
  _collection_state = CollectionState::UNCONFIGURED;
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
 */
auto LibsensorsCollector::StartCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  auto             result = ASTL_STATUS_SUCCESS;
  if (_collection_state == CollectionState::STARTED) {
    result = ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  } else if ((_collection_state != CollectionState::CONFIGURED && _collection_state != CollectionState::STOPPED) ||
             !_configuration.has_value()) {
    result = ASTL_STATUS_BAD_CONFIGURATION;  // Cannot start while already started or unconfigured
  } else {
    result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStart);
    if (result == ASTL_STATUS_SUCCESS) {
      switch (_configuration->CollectionParams().collection_mode) {
        case ASTL_COLLECTION_MODE_IMMEDIATE:
          // Immediate mode does not require any special setup, we will collect data on ReadImmediate calls
          break;
        case ASTL_COLLECTION_MODE_SNAPSHOT:
          // Snapshot mode samples data on Start and Stop calls
          result = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
          break;
        case ASTL_COLLECTION_MODE_SAMPLING:
          // Sampling mode requires setting up a timer or similar mechanism to periodically collect data
          result = StartIntervalSampling();
          break;
        default:
          result = ASTL_STATUS_BAD_CONFIGURATION;  // Unsupported collection mode
          break;
      }
    }
    if (result == ASTL_STATUS_SUCCESS) {
      _collection_state = CollectionState::STARTED;
    }
  }
  return result;
}

/*
 * @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact.
 */
auto LibsensorsCollector::PauseCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("PauseCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Pause();
  }
  if (_collection_state == CollectionState::STARTED) {
    _collection_state = CollectionState::PAUSED;
  }
  return EmitPauseResumeSample(PauseResumeMarker::PAUSE, ClockMonotonicRaw::now());
};

/*
 * @brief Resume the collection of data, starting any async tasks
 */
auto LibsensorsCollector::ResumeCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  // Emit the resume marker before restarting the periodic sampler so the marker timestamp
  // strictly precedes any new samples produced after the sampler resumes.
  auto       resume_timestamp = ClockMonotonicRaw::now();
  const auto emit_status      = EmitPauseResumeSample(PauseResumeMarker::RESUME, resume_timestamp);
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("ResumeCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Resume();
  }
  if (_collection_state == CollectionState::PAUSED) {
    _collection_state = CollectionState::STARTED;
  }
  return emit_status;
};

/*
 * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
 */
auto LibsensorsCollector::StopCollection() -> astl_status_code {
  // before we modify any of the collection configuration state, try to stop any
  // configured interval sampling. taking the _collection_mutex lock here has contention
  // against the sampling thread, so signal the stop _BEFORE_ grabbing _collection_mutex.
  StopIntervalSampling();
  auto             result = ASTL_STATUS_SUCCESS;
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STOPPED) {
    return ASTL_STATUS_COLLECTION_ALREADY_STOPPED;  // stop is idempotent
  }
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot stop while not started, paused, or unconfigured
  }
  switch (_configuration->CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      // Immediate mode does not require any special teardown
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      // Snapshot mode samples data on Start and Stop calls
      result = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      if (result != ASTL_STATUS_SUCCESS) {
        return result;
      }
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      // NOTE - we should already have stopped this interval sampling
      // at the top of this function before grabbing _collection_mutex
      break;
    default:
      return ASTL_STATUS_BAD_CONFIGURATION;  // Unsupported collection mode
  }
  result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStop);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;  // Propagate the error code from the operation
  }
  // Restore the original enabled state of data events
  _collection_state = CollectionState::STOPPED;
  return result;
}

/*
 * @brief Collect a single sample of all the configured metrics.
 */
auto LibsensorsCollector::ReadImmediate() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::CONFIGURED &&
       _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot read while unconfigured or stopped
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

/*
 * @brief Snapshot CLOCK_MONOTONIC_RAW and steady_clock simultaneously for every sample operation.
 *        All libsensors operations share steady_clock as their native clock, so one pair of
 *        snapshots is sufficient and is mapped to every OperationId.
 */
auto LibsensorsCollector::GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock lock{_collection_mutex};
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("LibsensorsCollector::GetNativeClockSnapshot called without configuration; returning empty map");
    return {};
  }
  // Take both clock snapshots as close together as possible
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

/*
 * @brief Casts a sequence of abstract operations and executes them.
 */
auto LibsensorsCollector::ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code {
  std::vector<RawSampledData> collected_samples;
  collected_samples.reserve(operations.size());

  for (const auto& operation_ptr : operations) {
    const auto* sensors_operation = dynamic_cast<LibsensorsReadOperation*>(operation_ptr.get());
    if (!sensors_operation) {
      return ASTL_STATUS_BAD_ARGUMENT;  // Invalid operation type
    }

    const sensors_chip_name* chip              = sensors_operation->chip;
    const int                subfeature_number = sensors_operation->subfeature_number;
    double                   measured_value{0.0};
    // read the value from the sensors api
    int result = _sensors_api->get_value(chip, sensors_operation->subfeature_number, &measured_value);
    if (result != 0) {
      ASTL_LOG_CRITICAL("Error {} executing sensor read operation for subfeature {}", result, subfeature_number);
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    auto timestamp = static_cast<uint64_t>(
        std::chrono::time_point_cast<SampleMicroseconds>(std::chrono::steady_clock::now()).time_since_epoch().count());
    collected_samples.emplace_back(operation_ptr->GetId(), AstlValue{measured_value}, timestamp);
  }

  if (!collected_samples.empty() && _sample_sink) {
    return _sample_sink->SinkRawSamples(_configuration->Target(), collected_samples);
  }

  return ASTL_STATUS_SUCCESS;  // Successfully read and sent the samples
}

auto LibsensorsCollector::CheckStartIntervalSampling() const -> astl_status_code {
  return CheckPeriodicSamplerStart(_collection_state, _configuration,
                                   {CollectionState::CONFIGURED, CollectionState::STOPPED, CollectionState::PAUSED},
                                   "Interval sampling");
}

/*
 * @brief Initialize any threads or async tasks needed for interval sampling.
 */
auto LibsensorsCollector::StartIntervalSampling() -> astl_status_code {
  auto status = CheckStartIntervalSampling();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  return StartPeriodicSamplerForCollector(this, *_configuration, _periodic_sampler, "Interval sampling");
}

auto LibsensorsCollector::EmitPauseResumeSample(PauseResumeMarker marker_type, ProcessedSampleTimestamp timestamp)
    -> astl_status_code {
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("Marker sample emission skipped because collector has no active configuration");
    return ASTL_STATUS_SUCCESS;
  }
  if (_sample_sink == nullptr) {
    return ASTL_STATUS_SUCCESS;
  }
  auto marker = (marker_type == PauseResumeMarker::PAUSE) ? RawSampledData::PauseMarker(timestamp)
                                                          : RawSampledData::ResumeMarker(timestamp);
  return _sample_sink->SinkRawSamples(_configuration->Target(), {&marker, 1});
}

/*
 * @brief Stop any background threads or async tasks that were started for interval sampling.
 *
 * @returns ASTL_STATUS_SUCCESS even if there was no ongoing task. this is because
 *          we must try to signal _stop_sampling_task in a lightweight way with no lock
 */
auto LibsensorsCollector::StopIntervalSampling() -> void {
  _periodic_sampler = nullptr;  // destroy periodic_sampler and wait for its thread pool to empty
}

}  // namespace astl
