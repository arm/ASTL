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

#include "collector/libsensors_collector.hpp"

#include <atomic>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "astl_logger.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "operation/libsensors_read_operation.hpp"
#include "operation/operation.hpp"

namespace astl {

LibsensorsCollector::LibsensorsCollector() {}

/*
 * @brief Get the capabilities of this collector, including the collector type.
 */
CollectorCapability const& LibsensorsCollector::GetCapabilities() const { return _collector_capability; };

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any ISampleSink.
 */
void LibsensorsCollector::SetRawSampleSink(IRawSampleSink* raw_sample_sink) {
  std::scoped_lock lock{_collection_mutex};
  _sample_sink = raw_sample_sink;
};

/*
 * @brief Configure the collector to collect data, but don't start sampling it yet.
 *
 * @param configuration The configuration to apply to this collector, including the set of operations to run,
 *        the interval to sample at.
 */
astl_status_code LibsensorsCollector::ConfigureCollection(CollectionConfiguration&& configuration) {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot reconfigure while already started
  }
  _configuration    = std::move(configuration);
  _collection_state = CollectionState::STOPPED;
  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

/*
 * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
 */
astl_status_code LibsensorsCollector::StartCollection() {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  if (_collection_state != CollectionState::STOPPED || !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot start while already started or unconfigured
  }

  auto result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStart);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;  // Propagate the error code from the operation
  }

  switch (_configuration->CollectionParams()._collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      // Immediate mode does not require any special setup, we will collect data on ReadImmediate calls
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      // Snapshot mode samples data on Start and Stop calls
      result = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      if (result != ASTL_STATUS_SUCCESS) {
        return result;
      }
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      // Sampling mode requires setting up a timer or similar mechanism to periodically collect data
      result = StartIntervalSampling();
      if (result != ASTL_STATUS_SUCCESS) {
        return result;
      }
      break;
    default:
      return ASTL_STATUS_BAD_CONFIGURATION;  // Unsupported collection mode
  }
  _collection_state = CollectionState::STARTED;
  return result;
}

/*
 * @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact.
 */
astl_status_code LibsensorsCollector::PauseCollection() {
  std::scoped_lock lock{_collection_mutex};
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("PauseCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Pause();
  }
  return ASTL_STATUS_SUCCESS;
};

/*
 * @brief Resume the collection of data, starting any async tasks
 */
astl_status_code LibsensorsCollector::ResumeCollection() {
  std::scoped_lock lock{_collection_mutex};
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("ResumeCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Unpause();
  }
  return ASTL_STATUS_SUCCESS;
};

/*
 * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
 */
astl_status_code LibsensorsCollector::StopCollection() {
  // before we modify any of the collection configuration state, try to stop any
  // configured interval sampling. taking the _collection_mutex lock here has contention
  // against the sampling thread, so signal the stop _BEFORE_ grabbing _collection_mutex.
  StopIntervalSampling();
  auto             result = ASTL_STATUS_SUCCESS;
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STOPPED) {
    return ASTL_STATUS_COLLECTION_ALREADY_STOPPED;  // stop is idempotent
  }
  if (_collection_state != CollectionState::STARTED || !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot stop while not started or unconfigured
  }
  switch (_configuration->CollectionParams()._collection_mode) {
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
 * @brief Collect a single sample of all the configured metics.
 */
astl_status_code LibsensorsCollector::ReadImmediate() {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::STARTED) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot read while not started
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

/*
 * @brief Casts a sequence of abstract operations and executes them.
 */
astl_status_code LibsensorsCollector::ExecuteCollectionOperations(OperationSequence const& operations) {
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
    int                      result = sensors_get_value(chip, sensors_operation->subfeature_number, &measured_value);
    if (result != 0) {
      ASTL_LOG_CRITICAL("Error {} executing sensor read operation for subfeature {}", result, subfeature_number);
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    auto timestamp = std::chrono::time_point_cast<SampleTimestamp::duration>(SampleTimestamp::clock::now());
    collected_samples.emplace_back(operation_ptr->GetId(), AstlValue{measured_value}, timestamp);
  }

  if (!collected_samples.empty() && _sample_sink) {
    return _sample_sink->SinkRawSamples(_configuration->Target(), collected_samples);
  }

  return ASTL_STATUS_SUCCESS;  // Successfully read and sent the samples
}

/*
 * @brief Initialize any threads or async tasks needed for interval sampling.
 */
astl_status_code LibsensorsCollector::StartIntervalSampling() {
  if (_collection_state != CollectionState::STOPPED && _collection_state != CollectionState::PAUSED) {
    ASTL_LOG_ERROR("Interval sampling started when collection state is not stopped or paused");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_configuration.has_value()) {
    ASTL_LOG_ERROR("Interval sampling start attempted with no configuration!");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (_periodic_sampler) {
    ASTL_LOG_ERROR("Interval sampling started _periodic_sampler is already initialized");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto interval     = std::chrono::milliseconds{_configuration.value().CollectionParams()._sampling_interval};
  _periodic_sampler = std::make_unique<PeriodicSampler>(this, interval);
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Stop any background threads or async tasks that were started for interval sampling.
 *
 * @returns ASTL_STATUS_SUCCESS even if there was no ongoing task. this is because
 *          we must try to signal _stop_sampling_task in a lightweight way with no lock
 */
void LibsensorsCollector::StopIntervalSampling() {
  _periodic_sampler = nullptr;  // destroy periodic_sampler and wait for its thread pool to empty
}

}  // namespace astl
