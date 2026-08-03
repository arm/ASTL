// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/scmi_ioctl_collector.hpp"

#include <filesystem>
#include <utility>

#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "collector/scmi_operation_helpers.hpp"

namespace astl {

/**
 * @brief Creates an ioctl-backed SCMI collector for one telemetry character device.
 *
 * @param device_path Path to a telemetry character device, such as `/dev/scmi/tlm_0`.
 */
ScmiIoctlCollector::ScmiIoctlCollector(std::filesystem::path device_path)
    : _scmi_ioctl_interface{std::make_unique<ScmiIoctlInterface>(std::move(device_path))} {}

/**
 * @brief Creates a collector using a caller-provided SCMI ioctl interface implementation.
 *
 * @param scmi_ioctl_interface Interface implementation used for all SCMI ioctl operations.
 */
ScmiIoctlCollector::ScmiIoctlCollector(std::unique_ptr<IScmiIoctlInterface> scmi_ioctl_interface)
    : _scmi_ioctl_interface{std::move(scmi_ioctl_interface)} {}

/**
 * @brief Stops active collection and restores modified data event state before destruction.
 */
ScmiIoctlCollector::~ScmiIoctlCollector() {
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    ASTL_LOG_WARNING(
        "ScmiIoctlCollector destroyed while collection is active or paused. Forcing StopCollection for cleanup.");
    StopCollection();
  }
  std::scoped_lock lock{_collection_mutex};
  RollbackConfigurationState("collector destruction");
}

/**
 * @brief Reports the collector capability advertised for SCMI targets.
 *
 * @return SCMI collector capability descriptor.
 */
auto ScmiIoctlCollector::GetCapabilities() const -> CollectorCapability { return _collector_capability; }

/**
 * @brief Registers the sink that receives raw samples produced by this collector.
 *
 * @param raw_sample_sink Sink to receive raw samples, or null to disable forwarding.
 */
auto ScmiIoctlCollector::SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void {
  std::scoped_lock lock{_collection_mutex};
  _raw_sample_sink = raw_sample_sink;
}

/**
 * @brief Enables target-level telemetry through SCMI_TLM_GET_CFG and SCMI_TLM_SET_CFG.
 *
 * @return ASTL_STATUS_SUCCESS on success, or an ioctl failure status.
 */
auto ScmiIoctlCollector::EnableTelemetry() -> astl_status_code {
  scmi_tlm_config config{};
  auto            status = _scmi_ioctl_interface->GetConfig(config);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (config.enable != 0) {
    return ASTL_STATUS_SUCCESS;
  }
  config.enable = 1;
  return _scmi_ioctl_interface->SetConfig(config);
}

/**
 * @brief Configures collection operations and enables their required SCMI data events.
 *
 * @param configuration Collection configuration containing the target, parameters, and operations.
 * @return ASTL_STATUS_SUCCESS on success, or the first setup failure.
 */
auto ScmiIoctlCollector::ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::CONFIGURED &&
      _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  if (_collection_state == CollectionState::CONFIGURED) {
    RollbackConfigurationState("ConfigureCollection replacement");
  }
  _use_software_clock_timestamps = IsEnvVarSet(EnvVar::ASTL_SCMI_USE_SOFTWARE_CLOCK_TIMESTAMPS);
  ASTL_LOG_INFO("ScmiIoctlCollector: use_software_clock_timestamps={}", _use_software_clock_timestamps);

  scmi_tlm_abi_info abi_info{};
  auto              result = _scmi_ioctl_interface->GetAbiInfo(abi_info);
  if (result != ASTL_STATUS_SUCCESS || !ScmiTlmAbiInfoIsCompatible(abi_info)) {
    if (result == ASTL_STATUS_SUCCESS) {
      result = ASTL_STATUS_NOT_SUPPORTED;
    }
    ASTL_LOG_ERROR("Error {} negotiating the SCMI telemetry ioctl ABI for device '{}'", astl::to_string(result),
                   _scmi_ioctl_interface->DevicePath().string());
    RollbackConfigurationState("ConfigureCollection failure");
    return result;
  }
  _abi_info = abi_info;

  ASTL_LOG_INFO(
      "SCMI telemetry ioctl ABI: version={}, abi_features=0x{:08X}, instance_features=0x{:08X}, "
      "num_des={}, num_groups={}, num_intervals={}, num_shmtis={}, de_implementation_version={}",
      abi_info.abi_version, abi_info.abi_features, abi_info.features, abi_info.num_des, abi_info.num_groups,
      abi_info.num_intervals, abi_info.num_shmtis, ScmiIoctlInterface::FormatDeImplementationVersion(abi_info));
  ASTL_LOG_INFO("SCMI telemetry ioctl capabilities: reset={}, single_read={}, group_config={}, update_notification={}",
                ScmiTlmCanReset(abi_info), ScmiTlmHasInstanceFeature(abi_info, SCMI_TLM_BASE_SUPPORT_SINGLE_SAMPLE),
                ScmiTlmHasInstanceFeature(abi_info, SCMI_TLM_BASE_SUPPORT_GROUP_CONFIG),
                ScmiTlmHasInstanceFeature(abi_info, SCMI_TLM_BASE_SUPPORT_UPDATE_NOTIFICATION));
  if (abi_info.abi_version > SCMI_TLM_CURRENT_ABI_VERSION) {
    ASTL_LOG_INFO("SCMI telemetry ioctl ABI version {} is newer than ASTL's version {}; using the compatible V1 prefix",
                  abi_info.abi_version, SCMI_TLM_CURRENT_ABI_VERSION);
  }
  const auto unknown_abi_features      = abi_info.abi_features & ~kScmiTlmKnownAbiFeatures;
  const auto unknown_instance_features = abi_info.features & ~kScmiTlmKnownInstanceFeatures;
  if (unknown_abi_features != 0 || unknown_instance_features != 0) {
    ASTL_LOG_INFO("SCMI telemetry ioctl exposes future capabilities: abi=0x{:08X}, instance=0x{:08X}",
                  unknown_abi_features, unknown_instance_features);
  }

  result = EnableTelemetry();
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Error {} enabling SCMI Telemetry ioctl device '{}'", astl::to_string(result),
                   _scmi_ioctl_interface->DevicePath().string());
    RollbackConfigurationState("ConfigureCollection failure");
    return result;
  }

  _configuration          = std::move(configuration);
  _collection_state       = CollectionState::CONFIGURED;
  auto all_data_event_ids = scmi_operation_helpers::GetUniqueDataEventIds(_configuration->Operations());
  auto data_events        = EnableDataEvents(all_data_event_ids);
  if (!data_events) {
    RollbackConfigurationState("ConfigureCollection failure");
    return data_events.error();
  }
  _data_events = *data_events;

  scmi_operation_helpers::UpdateReadOperationTimestampRates(_data_events, _configuration->Operations());

  result = ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
  if (result != ASTL_STATUS_SUCCESS) {
    RollbackConfigurationState("ConfigureCollection failure");
  }
  return result;
}

/**
 * @brief Clears configured collection state when collection is not active.
 *
 * @return ASTL_STATUS_SUCCESS on success, or ASTL_STATUS_COLLECTION_ALREADY_RUNNING when active.
 */
auto ScmiIoctlCollector::ClearCollectionState() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  StopIntervalSampling();
  RollbackConfigurationState("ClearCollectionState");
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Starts the configured collection according to the selected collection mode.
 *
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status describing why start failed.
 */
auto ScmiIoctlCollector::StartCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  auto             result = ASTL_STATUS_SUCCESS;
  if (_collection_state == CollectionState::STARTED) {
    result = ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  } else if ((_collection_state != CollectionState::CONFIGURED && _collection_state != CollectionState::STOPPED) ||
             !_configuration.has_value()) {
    result = ASTL_STATUS_BAD_CONFIGURATION;
  } else {
    _previous_timestamps.clear();

    result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStart);
    if (result == ASTL_STATUS_SUCCESS) {
      switch (_configuration->CollectionParams().collection_mode) {
        case ASTL_COLLECTION_MODE_IMMEDIATE:
          break;
        case ASTL_COLLECTION_MODE_SNAPSHOT:
          result = SampleCollectionOperations(_configuration->Operations().operationsOnSample);
          break;
        case ASTL_COLLECTION_MODE_SAMPLING:
          result = StartIntervalSampling();
          break;
        default:
          result = ASTL_STATUS_BAD_CONFIGURATION;
          break;
      }
    }
    if (result == ASTL_STATUS_SUCCESS) {
      _collection_state = CollectionState::STARTED;
    }
  }
  return result;
}

/**
 * @brief Pauses periodic sampling and emits a pause marker sample.
 *
 * @return ASTL_STATUS_SUCCESS on success, or a sink failure status.
 */
auto ScmiIoctlCollector::PauseCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("PauseCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Pause();
  }
  if (_collection_state == CollectionState::STARTED) {
    _collection_state = CollectionState::PAUSED;
  }
  auto pause_timestamp = ClockMonotonicRaw::now();
  return EmitPauseResumeSample(PauseResumeMarker::PAUSE, pause_timestamp);
}

/**
 * @brief Emits a resume marker sample and resumes periodic sampling.
 *
 * @return ASTL_STATUS_SUCCESS on success, or a sink failure status.
 */
auto ScmiIoctlCollector::ResumeCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  auto             resume_timestamp = ClockMonotonicRaw::now();
  const auto       emit_status      = EmitPauseResumeSample(PauseResumeMarker::RESUME, resume_timestamp);
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("ResumeCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Resume();
  }
  if (_collection_state == CollectionState::PAUSED) {
    _collection_state = CollectionState::STARTED;
  }
  return emit_status;
}

/**
 * @brief Stops collection, runs stop operations, and restores modified data event state.
 *
 * @return ASTL_STATUS_SUCCESS on success, or the first stop or restore failure.
 */
auto ScmiIoctlCollector::StopCollection() -> astl_status_code {
  StopIntervalSampling();
  auto             result = ASTL_STATUS_SUCCESS;
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STOPPED) {
    result = ASTL_STATUS_COLLECTION_ALREADY_STOPPED;
  } else if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::PAUSED) ||
             !_configuration.has_value()) {
    result = ASTL_STATUS_BAD_CONFIGURATION;
  } else {
    switch (_configuration->CollectionParams().collection_mode) {
      case ASTL_COLLECTION_MODE_IMMEDIATE:
        break;
      case ASTL_COLLECTION_MODE_SNAPSHOT:
        result = SampleCollectionOperations(_configuration->Operations().operationsOnSample);
        break;
      case ASTL_COLLECTION_MODE_SAMPLING:
        break;
      default:
        result = ASTL_STATUS_BAD_CONFIGURATION;
        break;
    }
  }

  if (result == ASTL_STATUS_SUCCESS) {
    result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStop);
  }
  if (result == ASTL_STATUS_SUCCESS) {
    result = RestoreDataEventEnabledState(_data_events);
    _data_events.clear();
    _previous_timestamps.clear();
    _collection_state = CollectionState::STOPPED;
  }
  return result;
}

/**
 * @brief Restores modified state after configuration failure or cleanup.
 *
 * @param rollback_context Context string used in warning logs.
 */
void ScmiIoctlCollector::RollbackConfigurationState(const char* rollback_context) noexcept {
  if (!_data_events.empty()) {
    auto restore_result = RestoreDataEventEnabledState(_data_events);
    if (restore_result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_WARNING("Failed to roll back SCMI ioctl data event state after {}: {}", rollback_context,
                       astl::to_string(restore_result));
    }
    _data_events.clear();
  }
  _previous_timestamps.clear();
  _configuration.reset();
  _collection_state = CollectionState::UNCONFIGURED;
}

}  // namespace astl
