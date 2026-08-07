// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <expected>

#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "collector/scmi_ioctl_collector.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl {

/**
 * @brief Executes the configured sample operations once for immediate reads.
 *
 * @return ASTL_STATUS_SUCCESS on success, or an operation failure status.
 */
auto ScmiIoctlCollector::ReadImmediate() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::CONFIGURED &&
       _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return SampleCollectionOperations(_configuration->Operations().operationsOnSample);
}

/**
 * @brief Builds clock-correlation snapshots for configured SCMI read operations.
 *
 * @return Clock correlation map, or an ASTL status when an ioctl read fails.
 */
auto ScmiIoctlCollector::GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock lock{_collection_mutex};
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("ScmiIoctlCollector::GetNativeClockSnapshot called without configuration; returning empty map");
    return {};
  }
  ClockCorrelationMap result;
  for (const auto& operation_ptr : _configuration->Operations().operationsOnSample) {
    const auto* scmi_op = dynamic_cast<const ScmiReadOperation*>(operation_ptr.get());
    if (!scmi_op) {
      continue;
    }
    if (_use_software_clock_timestamps) {
      const auto raw_now       = ClockMonotonicRaw::now();
      const auto native_anchor = static_cast<HwClockTicks>(raw_now.time_since_epoch().count());
      result[scmi_op->GetId()] = OperationClockCorrelation{
          raw_now, native_anchor, NativeToMonotonicRawRatio{1, 1}
      };
      continue;
    }

    scmi_tlm_de_sample sample{};
    const auto         read_result = _scmi_ioctl_interface->ReadDataEventValue(scmi_op->scmi_data_event_id, sample);
    const auto         raw_now     = ClockMonotonicRaw::now();
    if (read_result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ScmiIoctlCollector::GetNativeClockSnapshot: ioctl read failed for DE_ID {:04X}",
                     scmi_op->scmi_data_event_id);
      return std::unexpected{read_result};
    }
    const auto raw_ratio     = NativeToMonotonicRawRatio{1'000'000LL, static_cast<intmax_t>(scmi_op->tstamp_rate)};
    result[scmi_op->GetId()] = OperationClockCorrelation{raw_now, sample.tstamp, raw_ratio};
  }
  return result;
}

/**
 * @brief Executes a sequence of configured SCMI read operations.
 *
 * @param operations Operations to execute in order.
 * @return ASTL_STATUS_SUCCESS on success, or the first operation failure.
 */
auto ScmiIoctlCollector::ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code {
  for (const auto& operation_ptr : operations) {
    const auto* scmi_operation = dynamic_cast<ScmiReadOperation*>(operation_ptr.get());
    if (!scmi_operation) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    auto result = ExecuteScmiReadOperation(*scmi_operation);
    if (result != ASTL_STATUS_SUCCESS) {
      return result;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Samples a sequence, using a platform-triggered single read when supported.
 */
auto ScmiIoctlCollector::SampleCollectionOperations(OperationSequence const& operations) -> astl_status_code {
  if (operations.empty()) {
    return ASTL_STATUS_SUCCESS;
  }
  const auto supports_single_read = _scmi_ioctl_interface->SupportsSingleRead();
  if (!supports_single_read) {
    return supports_single_read.error();
  }
  if (*supports_single_read) {
    return ExecuteSingleReadOperations(operations);
  }
  return ExecuteCollectionOperations(operations);
}

/**
 * @brief Reads one SCMI data event and forwards the resulting raw sample.
 *
 * @param operation SCMI read operation describing the data event and sample id.
 * @return ASTL_STATUS_SUCCESS on success, or an ioctl or sink failure status.
 */
auto ScmiIoctlCollector::ExecuteScmiReadOperation(ScmiReadOperation const& operation) -> astl_status_code {
  ASTL_LOG_TRACE("Executing SCMI ioctl read operation for data event ID: {:04X}", operation.scmi_data_event_id);
  scmi_tlm_de_sample sample{};
  auto               result = _scmi_ioctl_interface->ReadDataEventValue(operation.scmi_data_event_id, sample);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_CRITICAL("Error {} executing SCMI ioctl read operation for data event ID: {:04X}",
                      astlStatusString(result), operation.scmi_data_event_id);
    return result;
  }

  return EmitScmiSample(operation, sample);
}

/**
 * @brief Converts one ioctl sample into an ASTL raw sample and forwards it to the sink.
 */
auto ScmiIoctlCollector::EmitScmiSample(ScmiReadOperation const& operation, const scmi_tlm_de_sample& sample)
    -> astl_status_code {
  const auto timestamp = _use_software_clock_timestamps
                             ? static_cast<HwClockTicks>(ClockMonotonicRaw::now().time_since_epoch().count())
                             : sample.tstamp;
  auto       raw_value = AstlValue{sample.val};

  auto prev_timestamp_it = _previous_timestamps.find(operation.scmi_data_event_id);
  if (prev_timestamp_it != _previous_timestamps.end() && prev_timestamp_it->second == timestamp) {
    ASTL_LOG_WARNING(
        "ScmiIoctlCollector: discarding sample with duplicate timestamp for data event ID: {:04X}, timestamp: {}",
        operation.scmi_data_event_id, timestamp);
    return ASTL_STATUS_SUCCESS;
  }

  _previous_timestamps[operation.scmi_data_event_id] = timestamp;

  RawSampledData raw_sampled_data{operation.GetId(), raw_value, timestamp};
  if (_raw_sample_sink) {
    return _raw_sample_sink->SinkRawSamples(_configuration->Target(), {&raw_sampled_data, 1});
  }

  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Validates that periodic sampling can start from the current collector state.
 *
 * @return ASTL_STATUS_SUCCESS when sampling can start, otherwise ASTL_STATUS_BAD_CONFIGURATION.
 */
auto ScmiIoctlCollector::CheckStartIntervalSampling() const -> astl_status_code {
  return CheckPeriodicSamplerStart(_collection_state, _configuration,
                                   {CollectionState::CONFIGURED, CollectionState::STOPPED, CollectionState::PAUSED},
                                   "SCMI ioctl interval sampling");
}

/**
 * @brief Starts the periodic sampler for sampling-mode collection.
 *
 * @return ASTL_STATUS_SUCCESS on success, or a sampler startup failure.
 */
auto ScmiIoctlCollector::StartIntervalSampling() -> astl_status_code {
  auto status = CheckStartIntervalSampling();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  return StartPeriodicSamplerForCollector(this, *_configuration, _periodic_sampler, "SCMI ioctl interval sampling");
}

/**
 * @brief Emits a pause or resume marker sample to the registered raw sample sink.
 *
 * @param marker_type Marker type to emit.
 * @param timestamp Monotonic raw timestamp associated with the marker.
 * @return ASTL_STATUS_SUCCESS on success, or a sink failure status.
 */
auto ScmiIoctlCollector::EmitPauseResumeSample(PauseResumeMarker marker_type, ProcessedSampleTimestamp timestamp)
    -> astl_status_code {
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("Marker sample emission skipped because collector has no active configuration");
    return ASTL_STATUS_SUCCESS;
  }
  if (_raw_sample_sink == nullptr) {
    return ASTL_STATUS_SUCCESS;
  }
  auto marker = (marker_type == PauseResumeMarker::PAUSE) ? RawSampledData::PauseMarker(timestamp)
                                                          : RawSampledData::ResumeMarker(timestamp);
  return _raw_sample_sink->SinkRawSamples(_configuration->Target(), {&marker, 1});
}

/**
 * @brief Stops periodic sampling by releasing the active sampler.
 */
void ScmiIoctlCollector::StopIntervalSampling() { _periodic_sampler = nullptr; }

}  // namespace astl
