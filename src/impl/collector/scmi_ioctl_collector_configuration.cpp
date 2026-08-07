// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "collector/scmi_ioctl_collector.hpp"
#include "collector/scmi_operation_helpers.hpp"

namespace astl {

auto ScmiIoctlCollector::ProbeCapabilities() -> astl_status_code {
  const auto probe_status = _scmi_ioctl_interface->Probe();
  if (probe_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Error {} negotiating the SCMI telemetry ioctl ABI for device '{}'", astl::to_string(probe_status),
                   _scmi_ioctl_interface->DevicePath().string());
    return probe_status;
  }

  const auto de_implementation_version = _scmi_ioctl_interface->DeImplementationVersion();
  if (!de_implementation_version) {
    return de_implementation_version.error();
  }
  const auto data_event_count = _scmi_ioctl_interface->DataEventCount();
  if (!data_event_count) {
    return data_event_count.error();
  }
  const auto supports_single_read = _scmi_ioctl_interface->SupportsSingleRead();
  if (!supports_single_read) {
    return supports_single_read.error();
  }

  ASTL_LOG_INFO("SCMI telemetry ioctl: de_implementation_version={}, num_des={}, single_read={}",
                *de_implementation_version, *data_event_count, *supports_single_read);
  return ASTL_STATUS_SUCCESS;
}

auto ScmiIoctlCollector::PrepareConfiguration(CollectionConfiguration&& configuration) -> astl_status_code {
  const auto telemetry_status = EnableTelemetry();
  if (telemetry_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Error {} enabling SCMI Telemetry ioctl device '{}'", astl::to_string(telemetry_status),
                   _scmi_ioctl_interface->DevicePath().string());
    return telemetry_status;
  }

  _configuration          = std::move(configuration);
  _collection_state       = CollectionState::CONFIGURED;
  auto all_data_event_ids = scmi_operation_helpers::GetUniqueDataEventIds(_configuration->Operations());
  auto data_events        = EnableDataEvents(all_data_event_ids);
  if (!data_events) {
    return data_events.error();
  }

  _data_events = *data_events;
  scmi_operation_helpers::UpdateReadOperationTimestampRates(_data_events, _configuration->Operations());
  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

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

  auto result = ProbeCapabilities();
  if (result == ASTL_STATUS_SUCCESS) {
    result = PrepareConfiguration(std::move(configuration));
  }
  if (result != ASTL_STATUS_SUCCESS) {
    RollbackConfigurationState("ConfigureCollection failure");
  }
  return result;
}

}  // namespace astl
