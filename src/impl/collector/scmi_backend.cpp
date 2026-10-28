// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "astl_file_interface.hpp"
#include "collector/collector_backend.hpp"
#include "collector/scmi_backend_selection.hpp"
#include "collector/scmi_ioctl_collector.hpp"
#include "collector/scmi_ioctl_interface.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#include "metric/scmi_metric_builder.hpp"
#include "metric/scmi_target_configuration.hpp"
#include "topology/scmi_topology_plugin.hpp"

namespace astl {
namespace {
using ScmiSysfsFileCollector = ScmiSysfsCollector<FileInterface>;
auto BuildScmiCollector(const ITarget& target, const AstlConfiguration& configuration)
    -> std::expected<std::unique_ptr<ICollector>, astl_status_code> {
  const auto scmi_target_directory = target.CollectorTargetPath();
  if (!scmi_target_directory.has_value() || scmi_target_directory->empty()) {
    ASTL_LOG_ERROR("BuildCollectorManager: SCMI target '{}' is missing collector path metadata", target.Name());
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  const auto backend_preference = GetScmiBackendPreference();
  const auto ioctl_device_path  = ScmiIoctlInterface::DevicePathFromTelemetrySubdirectory(
      configuration.scmi_ioctl_device_root_path, *scmi_target_directory);
  const auto ioctl_available = ScmiPreferenceAllowsIoctl(backend_preference)
                                   ? ScmiIoctlTargetAvailable(ioctl_device_path)
                                   : std::expected<bool, astl_status_code>{false};

  if ((ioctl_available && *ioctl_available) || backend_preference == ScmiBackendPreference::IOCTL) {
    ASTL_LOG_INFO("BuildCollectorManager: using SCMI ioctl collector for target '{}' at {}", target.Name(),
                  ioctl_device_path.string());
    return std::make_unique<ScmiIoctlCollector>(ioctl_device_path);
  }

  const std::filesystem::path scmi_target_path =
      configuration.scmi_sysfs_telemetry_root_path / std::string{*scmi_target_directory};
  FileInterface scmi_target_file_interface{scmi_target_path};
  ASTL_LOG_INFO("BuildCollectorManager: using SCMI sysfs collector for target '{}' at {}", target.Name(),
                scmi_target_path.string());
  return std::make_unique<ScmiSysfsFileCollector>(std::move(scmi_target_file_interface));
}

}  // namespace
extern const CollectorBackend kScmiBackend{
    CollectorType::SCMI,           "scmi", ScmiTopologyPlugin::ScanForTargets, BuildScmiCollector, RegisterScmiMetrics,
    ApplyConfiguredScmiTargetNames};
}  // namespace astl
