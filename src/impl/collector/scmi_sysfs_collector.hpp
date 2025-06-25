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

#ifndef SCMI_SYSFS_COLLECTOR_HPP_
#define SCMI_SYSFS_COLLECTOR_HPP_

#include <atomic>
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "astl_logger.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "collector/scmi_data_event.hpp"
#include "common/capabilities.hpp"
#include "common/i_sample_sink.hpp"
#include "common/scmi/scmi_read_operation.hpp"

namespace astl {

/*
 * @brief A specialization of ICollector that interracts with the Scmi Sysfs system to fullfil SCMI operations
 *        in the case of multi-socket SCMI, there should be multiple instances of this collector,
 *        mapped in the Orchestrator by different Target instances
 */
template <typename FileInterfaceT>
class ScmiSysfsCollector : public ICollector {
 public:
  ~ScmiSysfsCollector() override = default;

  ScmiSysfsCollector() = delete;  // needs to be initialized with the base path for the telemetry directory
  ScmiSysfsCollector(ISampleSink* sample_sink, FileInterfaceT file_interface);

  ScmiSysfsCollector(const ScmiSysfsCollector&)            = default;
  ScmiSysfsCollector& operator=(const ScmiSysfsCollector&) = default;
  ScmiSysfsCollector(ScmiSysfsCollector&&)                 = default;
  ScmiSysfsCollector& operator=(ScmiSysfsCollector&&)      = default;

  /*
   * @brief Get the capabilities of this collector, including the collector type.
   */
  CollectorCapability const& GetCapabilities() const override;

  /*
   * @brief Set the destination for where sampled data should be sent.
   *       This is typically the CollectorManager, but can be any ISampleSink.
   */
  void SetSampleSink(ISampleSink* sample_sink) override;

  /*
   * @brief Configure the collector to collect data, but don't start sampling it yet.
   *
   * @param configuration The configuration to apply to this collector, including the set of operations to run,
   *        the interval to sample at.
   */
  astl_status_code ConfigureCollection(CollectionConfiguration&& configuration) override;

  /*
   * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
   */
  astl_status_code StartCollection() override;

  /*
   * @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact.
   */
  astl_status_code PauseCollection() override;

  /*
   * @brief Resume the collection of data, starting any async tasks
   */
  astl_status_code ResumeCollection() override;

  /*
   * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
   */
  astl_status_code StopCollection() override;

  /*
   * @brief Collect a single sample of all the configured metics.
   */
  astl_status_code ReadImmediate() override;

 private:
  // internal classes + enums

  enum class CollectionState { UNCONFIGURED, STOPPED, STARTED, PAUSED };

  // data members

  CollectorCapability _collector_capability{CollectorType::SCMI};  //!< The capabilities of this collector
  ISampleSink*        _sample_sink = nullptr;  //!< The (optional) destination for where sampled data should be sent
  CollectionState     _collection_state = CollectionState::UNCONFIGURED;
  std::optional<CollectionConfiguration> _configuration;  //!< The current active configuration for this collector
  FileInterfaceT                         _scmi_file_interface;
  //!< Data events touched by the current collection (TODO - consider making _collection_state a variant to bundle the
  //!< CollectionState in with _data_events and _configuration)
  std::vector<ScmiDataEvent> _data_events;
  mutable std::mutex
      _collection_mutex;  // prevent the collection configuration from being accessed by two threads at once
  std::unique_ptr<PeriodicSampler> _periodic_sampler;

  // private methods

  /*
   * @brief Enable the given data event ids, returning the set of DataEvents and their initial enable state
   */
  std::expected<std::vector<ScmiDataEvent>, astl_status_code> EnableDataEvents(
      std::unordered_set<ScmiDataEventId> const& data_events_to_enable);

  /*
   * @brief Restore the original 'enable' value, disabling all data events that were originally disabled
   *        Also restores the tstamp_enable file
   */
  astl_status_code RestoreDataEventEnabledState(std::vector<ScmiDataEvent> const& data_events);

  /*
   * @brief Casts a sequence of abstract operations to ScmiReadOperation and executes them.
   */
  astl_status_code ExecuteCollectionOperations(OperationSequence const& operations);

  /*
   * @brief Execute a single Scmi read operation, creates a new SampledData object from the read value
   * and sends it to the sample sink.
   */
  astl_status_code ExecuteScmiReadOperation(ScmiReadOperation const& operation);

  /*
   * @brief Initialize any threads or async tasks needed for interval sampling.
   */
  astl_status_code StartIntervalSampling();

  /*
   * @brief Stop any background threads or async tasks that were started for interval sampling.
   */
  void StopIntervalSampling();
};

////////////////////////////////////////////////////////////////////////////////
// Internal helpers, not part of this class's public API
////////////////////////////////////////////////////////////////////////////////
namespace scmi_detail {

std::expected<std::filesystem::path, astl_status_code> GetDataEventDirPath(ScmiDataEventId data_event_id);

std::expected<SampleTimestamp, astl_status_code> ParseScmiTimeStamp(std::string const& timestamp_str);

// Expected format: "<timestamp> <value>"
std::expected<std::pair<SampleTimestamp, ScmiDataEventValue>, astl_status_code> ParseDataEventValueWithTimestamp(
    std::string const& data_read);

// Expected format: "0 <value>"
std::expected<std::pair<SampleTimestamp, ScmiDataEventValue>, astl_status_code> ParseDataEventValueWithoutTimestamp(
    std::string const& data_read);

std::unordered_set<ScmiDataEventId> GetUniqueDataEventsIds(CollectionOperations const& operations);

}  // namespace scmi_detail

////////////////////////////////////////////////////////////////////////////////
// Implementations of ScmiSysfsCollector template class methods declared above
////////////////////////////////////////////////////////////////////////////////

template <typename FileInterfaceT>
ScmiSysfsCollector<FileInterfaceT>::ScmiSysfsCollector(ISampleSink* sample_sink, FileInterfaceT file_interface)
    : _sample_sink{sample_sink}, _scmi_file_interface{std::move(file_interface)} {}

/*
 * @brief Get the capabilities of this collector, including the collector type.
 */
template <typename FileInterfaceT>
CollectorCapability const& ScmiSysfsCollector<FileInterfaceT>::GetCapabilities() const {
  return _collector_capability;
};

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any ISampleSink.
 */
template <typename FileInterfaceT>
void ScmiSysfsCollector<FileInterfaceT>::SetSampleSink(ISampleSink* sample_sink) {
  std::scoped_lock lock{_collection_mutex};
  _sample_sink = sample_sink;
};

/*
 * @brief Configure the collector to collect data, but don't start sampling it yet.
 *
 * @param configuration The configuration to apply to this collector, including the set of operations to run,
 *        the interval to sample at.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ConfigureCollection(CollectionConfiguration&& configuration) {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot reconfigure while already started
  }
  // enable the telemetry subsystem
  auto result = _scmi_file_interface.Write(std::filesystem::path{kScmiTlmEnableFileName}, kScmiTlmEnableValue);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_CRITICAL("Error {} enabling SCMI Telemetry subsystem!", astl::to_string(result));
    return result;
  }

  _configuration          = std::move(configuration);
  _collection_state       = CollectionState::STOPPED;
  auto all_data_event_ids = scmi_detail::GetUniqueDataEventsIds(_configuration->Operations());
  auto data_events        = EnableDataEvents(all_data_event_ids);
  if (!data_events) {
    return data_events.error();
  }
  _data_events = *data_events;  // keep track of which data events were enabled during configuration

  // log some version info
  std::string de_implementation_version;
  result = _scmi_file_interface.Read(std::filesystem::path{kScmiInfoDirName} / kScmiInfoDeImplementationVersionFileName,
                                     de_implementation_version);
  ASTL_LOG_INFO("info/de_implementation_version: {}",
                result == ASTL_STATUS_SUCCESS ? de_implementation_version : astl::to_string(result));
  std::string telemetry_protocol_version;
  result =
      _scmi_file_interface.Read(std::filesystem::path{kScmiInfoDirName} / kScmiInfoVersion, de_implementation_version);
  ASTL_LOG_INFO("info/version: {}",
                result == ASTL_STATUS_SUCCESS ? telemetry_protocol_version : astl::to_string(result));

  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

/*
 * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::StartCollection() {
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
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::PauseCollection() {
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
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ResumeCollection() {
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
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::StopCollection() {
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
  result = RestoreDataEventEnabledState(_data_events);
  _data_events.clear();
  _collection_state = CollectionState::STOPPED;
  return result;
}

/*
 * @brief Collect a single sample of all the configured metics.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ReadImmediate() {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::STARTED) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot read while not started
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

/*
 * @brief Enable the given data event ids, returning the set of DataEvents and their initial enable state
 */
template <typename FileInterfaceT>
std::expected<std::vector<ScmiDataEvent>, astl_status_code> ScmiSysfsCollector<FileInterfaceT>::EnableDataEvents(
    std::unordered_set<ScmiDataEventId> const& data_events_to_enable) {
  std::vector<ScmiDataEvent> enabled_data_events;
  for (const auto& data_event_id : data_events_to_enable) {
    auto data_event_dir_path = scmi_detail::GetDataEventDirPath(data_event_id);
    if (!data_event_dir_path) {
      ASTL_LOG_ERROR("Failed to get data event directory path for ID: {}", data_event_id);
      return std::unexpected{ASTL_STATUS_FILE_OPEN_FAILED};
    }
    const auto enable_file_path = data_event_dir_path.value() / kScmiDataEventEnableFileName;
    // check to see if the data event is already enabled (if so, we don't disable it at the end of collection)
    std::string enabled_text;
    auto        result = _scmi_file_interface.Read(enable_file_path, enabled_text);
    if (result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to read enable file for data event ID: {:04X} with error: {}", data_event_id, result);
      return std::unexpected{result};
    }
    const bool originally_enabled = (enabled_text == kScmiDataEventEnableValue);
    // enable the data event if it's not already enabled.
    if (!originally_enabled) {
      result = _scmi_file_interface.Write(enable_file_path, kScmiDataEventEnableValue);
      if (result != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to enable data event ID: {:04X} with error: {}", data_event_id, result);
        return std::unexpected{result};
      }
    }
    // check to see if timestamps are enabled for this data event
    std::string tstamp_enabled_text;
    result = _scmi_file_interface.Read(data_event_dir_path.value() / kScmiDataEventTstampEnableFileName,
                                       tstamp_enabled_text);
    if (result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to read timestamp enable file for data event ID: {:04X} with error: {}", data_event_id,
                     result);
      return std::unexpected{result};
    }
    const bool timestamp_enabled = (tstamp_enabled_text == kScmiDataEventTstampEnableValue);
    if (!timestamp_enabled) {
      result = _scmi_file_interface.Write(data_event_dir_path.value() / kScmiDataEventTstampEnableFileName,
                                          kScmiDataEventTstampEnableValue);
    }

    enabled_data_events.emplace_back(data_event_id, originally_enabled, timestamp_enabled);
  }
  return enabled_data_events;
}

/*
 * @brief Restore the original 'enable' value, disabling all data events that were originally disabled
 *        Also restores the tstamp_enable file
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::RestoreDataEventEnabledState(
    std::vector<ScmiDataEvent> const& data_events) {
  for (const auto& data_event : data_events) {
    auto data_event_dir_path = scmi_detail::GetDataEventDirPath(data_event.id);
    if (!data_event_dir_path) {
      ASTL_LOG_ERROR("Failed to get data event directory path for ID: {}", data_event.id);
      return ASTL_STATUS_FILE_OPEN_FAILED;  // Return the error code from GetDataEventDirPath
    }
    // disable in reverse order: first disable timestamp, then event
    if (!data_event.timestamp_enabled) {
      // if the timestamp for this event wasn't enabled originally, disable it again now.
      auto result = _scmi_file_interface.Write(data_event_dir_path.value() / kScmiDataEventTstampEnableFileName,
                                               kScmiDataEventTstampDisableValue);
      if (result != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to disable timestamp for data event ID: {:04X} with error: {}", data_event.id, result);
        return result;  // Return the error code from Write
      }
    }
    if (!data_event.originally_enabled) {
      auto result = _scmi_file_interface.Write(data_event_dir_path.value() / kScmiDataEventEnableFileName,
                                               kScmiDataEventDisableValue);
      if (result != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to disable data event ID: {:04X} with error: {}", data_event.id, result);
        return result;  // Return the error code from Write
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Casts a sequence of abstract operations to ScmiReadOperation and executes them.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ExecuteCollectionOperations(OperationSequence const& operations) {
  for (const auto& operation_ptr : operations) {
    const auto* scmi_operation = dynamic_cast<ScmiReadOperation*>(operation_ptr.get());
    if (!scmi_operation) {
      return ASTL_STATUS_BAD_ARGUMENT;  // Invalid operation type
    }
    auto result = ExecuteScmiReadOperation(*scmi_operation);
    if (result != ASTL_STATUS_SUCCESS) {
      return result;  // Propagate the error code from the operation
    }
  }
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Executes a single ScmiReadOperation
 *        This will handle the actual file operations in the sysfs SCMI directory.
 *        creates a new SampledData object from the read value
 *        and sends it to the sample sink.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ExecuteScmiReadOperation(ScmiReadOperation const& operation) {
  auto data_event_dir_path = scmi_detail::GetDataEventDirPath(operation.scmi_data_event_id);
  if (!data_event_dir_path) {
    ASTL_LOG_CRITICAL("Error {} getting dir path for SCMI read operation for data event ID: {:04X}",
                      astl::to_string(data_event_dir_path.error()), operation.scmi_data_event_id);
    return data_event_dir_path.error();  // Return the error code from GetDataEventDirPath
  }
  ASTL_LOG_TRACE("Executing SCMI read operation for data event ID: {:04X}", operation.scmi_data_event_id);
  std::string data_read;
  auto        result = _scmi_file_interface.Read(data_event_dir_path.value() / kScmiDataEventValueFileName, data_read);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_CRITICAL("Error {} executing SCMI read operation for data event ID: {:04X}",
                      astl::to_string(data_event_dir_path.error()), operation.scmi_data_event_id);
    return result;
  }
  // TODO(https://github.com/Arm-Debug/ASTL/issues/92) - potentially disable timestamps depending on chosen optimization
  // flags
  auto parsed_value = scmi_detail::ParseDataEventValueWithTimestamp(data_read);
  if (!parsed_value) {
    return parsed_value.error();
  }
  auto        timestamp = parsed_value->first;
  auto        value     = parsed_value->second;
  SampledData sampled_data{operation.GetId(), value.AsAstlValue(), timestamp};

  if (_sample_sink) {
    return _sample_sink->SinkSamples(_configuration->Target(), {&sampled_data, 1});
  }

  return ASTL_STATUS_SUCCESS;  // Successfully read and sent the sample
}

/*
 * @brief Initialize any threads or async tasks needed for interval sampling.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::StartIntervalSampling() {
  if (_collection_state != CollectionState::STOPPED && _collection_state != CollectionState::PAUSED) {
    ASTL_LOG_ERROR("SCMI interval sampling started when collection state is not stopped or paused");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_configuration.has_value()) {
    ASTL_LOG_ERROR("SCMI interval sampling start attempted with no configuration!");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (_periodic_sampler) {
    ASTL_LOG_ERROR("SCMI interval sampling started _periodic_sampler is already initialized");
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
template <typename FileInterfaceT>
void ScmiSysfsCollector<FileInterfaceT>::StopIntervalSampling() {
  _periodic_sampler = nullptr;  // destroy periodic_sampler and wait for its thread pool to empty
}

}  // namespace astl

#endif  // SCMI_SYSFS_COLLECTOR_HPP_