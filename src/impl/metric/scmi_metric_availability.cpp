// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/scmi_metric_availability.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>

#include "astl_logger.hpp"
#include "collector/scmi_backend_selection.hpp"
#include "collector/scmi_ioctl_interface.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "metric/scmi_target_configuration.hpp"
#include "target.hpp"

namespace astl {

static auto GetScmiDataEventDirectoryPath(const AstlConfiguration& configuration, const ITarget& target,
                                          ScmiDataEventId data_event_id, bool wide_hex) -> std::filesystem::path {
  const auto telemetry_subdirectory = GetScmiTelemetrySubdirectory(target);
  const auto folder_name = wide_hex ? std::format("0x{:08X}", data_event_id) : std::format("0x{:04X}", data_event_id);
  return configuration.scmi_sysfs_telemetry_root_path / telemetry_subdirectory / "des" / folder_name;
}

static auto TryScmiIoctlDataEventAvailability(const AstlConfiguration& configuration, const MetricConfig& metric_config,
                                              const ITarget& target, ScmiDataEventId data_event_id,
                                              ScmiBackendPreference backend_preference) -> std::optional<bool> {
  std::optional<bool> data_event_exists;
  if (ScmiPreferenceAllowsIoctl(backend_preference)) {
    const auto telemetry_subdirectory = GetScmiTelemetrySubdirectory(target);
    const auto ioctl_device_path      = ScmiIoctlInterface::DevicePathFromTelemetrySubdirectory(
        configuration.scmi_ioctl_device_root_path, telemetry_subdirectory);
    const auto ioctl_available = ScmiIoctlTargetAvailable(ioctl_device_path);
    const bool use_ioctl = (ioctl_available && *ioctl_available) || backend_preference == ScmiBackendPreference::IOCTL;

    if (use_ioctl) {
      data_event_exists                 = false;
      const auto ioctl_data_event_found = ScmiIoctlDataEventExists(ioctl_device_path, data_event_id);
      if (ioctl_data_event_found && *ioctl_data_event_found) {
        data_event_exists = true;
      } else {
        ASTL_LOG_WARNING("Skipping SCMI metric '{}' (id: '{}') on target '{}' because ioctl DE 0x{:08X} is missing",
                         metric_config.Name(), metric_config.Id(), target.Name(), data_event_id);
      }
    }
  }
  return data_event_exists;
}

static auto ScmiSysfsDataEventExists(const AstlConfiguration& configuration, const MetricConfig& metric_config,
                                     const ITarget& target, ScmiDataEventId data_event_id) -> bool {
  const auto data_event_dir_path_wide   = GetScmiDataEventDirectoryPath(configuration, target, data_event_id, true);
  const auto data_event_dir_path_narrow = GetScmiDataEventDirectoryPath(configuration, target, data_event_id, false);
  std::error_code ec{};
  const bool      wide_exists = std::filesystem::exists(data_event_dir_path_wide, ec);
  ec.clear();
  const bool narrow_exists = std::filesystem::exists(data_event_dir_path_narrow, ec);
  const bool exists        = wide_exists || narrow_exists;
  if (!exists) {
    ASTL_LOG_WARNING(
        "Skipping SCMI metric '{}' (id: '{}') on target '{}' because DE directory '{}' (or legacy '{}') is missing",
        metric_config.Name(), metric_config.Id(), target.Name(), data_event_dir_path_wide.string(),
        data_event_dir_path_narrow.string());
  }
  return exists;
}

static auto ShouldSkipUnavailableScmiMetric(const AstlConfiguration& configuration, const MetricConfig& metric_config,
                                            const ScmiOperationBuilder* operation_builder, const ITarget* target)
    -> bool {
  auto should_skip = false;
  if (target != nullptr && target->GetCollectorType() == CollectorType::SCMI && operation_builder != nullptr) {
    const auto data_event_id      = operation_builder->GetDataEventId();
    const auto backend_preference = GetScmiBackendPreference();
    auto       ioctl_data_event_exists =
        TryScmiIoctlDataEventAvailability(configuration, metric_config, *target, data_event_id, backend_preference);
    if (ioctl_data_event_exists.has_value()) {
      should_skip = !*ioctl_data_event_exists;
    } else if (!ScmiPreferenceAllowsSysfs(backend_preference)) {
      should_skip = true;
    } else {
      should_skip = !ScmiSysfsDataEventExists(configuration, metric_config, *target, data_event_id);
    }
  }
  return should_skip;
}

auto FilterUnavailableScmiMetricConfigs(const AstlConfiguration& configuration, MetricConfigOnTargets& configs)
    -> void {
  std::erase_if(configs, [&configuration](auto& config_entry) {
    const auto& metric_config     = config_entry.first;
    auto&       targets           = config_entry.second;
    const auto* operation_builder = std::get_if<ScmiOperationBuilder>(&metric_config->GetOperationBuilder());

    std::erase_if(targets, [&](const ITarget* target) {
      return ShouldSkipUnavailableScmiMetric(configuration, *metric_config, operation_builder, target);
    });
    return targets.empty();
  });
}

}  // namespace astl
