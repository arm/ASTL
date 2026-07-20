// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <vector>

#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "collector/scmi_data_event.hpp"
#include "collector/scmi_ioctl_collector.hpp"

namespace astl {

/**
 * @brief Enables each required data event and records its original state for restoration.
 *
 * @param data_events_to_enable SCMI data event identifiers required by the configured operations.
 * @return Original data event states to restore later, or an ASTL status on failure.
 */
auto ScmiIoctlCollector::EnableDataEvents(std::unordered_set<ScmiDataEventId> const& data_events_to_enable)
    -> std::expected<std::vector<ScmiDataEvent>, astl_status_code> {
  std::vector<ScmiDataEvent> enabled_data_events;
  for (const auto& data_event_id : data_events_to_enable) {
    scmi_tlm_de_config original_config{};
    auto               result = _scmi_ioctl_interface->GetDataEventConfig(data_event_id, original_config);
    if (result != ASTL_STATUS_SUCCESS) {
      static_cast<void>(RestoreDataEventEnabledState(enabled_data_events));
      return std::unexpected{result};
    }

    scmi_tlm_de_info info{};
    result = _scmi_ioctl_interface->GetDataEventInfo(data_event_id, info);
    if (result != ASTL_STATUS_SUCCESS) {
      static_cast<void>(RestoreDataEventEnabledState(enabled_data_events));
      return std::unexpected{result};
    }

    const bool               originally_enabled = original_config.enable != 0;
    const bool               can_use_timestamps = !_use_software_clock_timestamps && info.ts_rate != 0;
    std::optional<bool>      original_timestamp_enabled;
    std::optional<kilohertz> timestamp_rate;
    if (can_use_timestamps) {
      original_timestamp_enabled = original_config.t_enable != 0;
      timestamp_rate             = kilohertz{info.ts_rate};
    }

    scmi_tlm_de_config new_config = original_config;
    new_config.enable             = 1;
    if (can_use_timestamps) {
      new_config.t_enable = 1;
    }
    result = _scmi_ioctl_interface->SetDataEventConfig(new_config);
    if (result != ASTL_STATUS_SUCCESS) {
      static_cast<void>(RestoreDataEventEnabledState(enabled_data_events));
      return std::unexpected{result};
    }

    enabled_data_events.emplace_back(data_event_id, originally_enabled, original_timestamp_enabled, timestamp_rate);
  }
  return enabled_data_events;
}

/**
 * @brief Restores data event enable and timestamp-enable state captured during configuration.
 *
 * @param data_events Original data event states captured before enabling collection.
 * @return ASTL_STATUS_SUCCESS on success, or the first restore failure.
 */
auto ScmiIoctlCollector::RestoreDataEventEnabledState(std::vector<ScmiDataEvent> const& data_events)
    -> astl_status_code {
  for (const auto& data_event : data_events) {
    scmi_tlm_de_config config{};
    auto               result = _scmi_ioctl_interface->GetDataEventConfig(data_event.id, config);
    if (result != ASTL_STATUS_SUCCESS) {
      return result;
    }
    config.enable = data_event.originally_enabled ? 1U : 0U;
    if (data_event.timestamp_enabled.has_value()) {
      config.t_enable = data_event.timestamp_enabled.value() ? 1U : 0U;
    }
    result = _scmi_ioctl_interface->SetDataEventConfig(config);
    if (result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to restore SCMI ioctl data event ID {:04X}: {}", data_event.id, astl::to_string(result));
      return result;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
