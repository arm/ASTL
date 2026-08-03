// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "topology/scmi_topology_plugin.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "astl_file_interface.hpp"
#include "astl_logger.hpp"
#include "collector/scmi_backend_selection.hpp"
#include "collector/scmi_ioctl_interface.hpp"
#include "config/astl_configuration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"
#include "target.hpp"
#include "topology/scmi_target.hpp"

namespace astl::ScmiTopologyPlugin {
namespace detail {

auto BuildTargetName(const std::string& telemetry_subdirectory) -> std::string {
  return ScmiTarget::NameForTelemetrySubdirectory(telemetry_subdirectory);
}

auto BuildSysfsTargetFromImplementationVersion(const std::string& implementation_version,
                                               const std::string& telemetry_subdirectory)
    -> std::expected<std::unique_ptr<ITarget>, astl_status_code> {
  const auto uuid_result = scmi::spec::GetNormalizedUuid(implementation_version);
  if (!uuid_result) {
    return std::unexpected(uuid_result.error());
  }

  const auto uuid = uuid_result.value();
  ASTL_LOG_INFO("ScmiTopologyPlugin::ScanForTargets: Successfully detected SCMI/SysFS target with UUID {}",
                uuid.normalized_value);
  const auto target_name = BuildTargetName(telemetry_subdirectory);
  auto target_ptr = std::make_unique<ScmiTarget>(target_name, "Target discovered via SCMI", telemetry_subdirectory,
                                                 nullptr, uuid.normalized_value);
  return target_ptr;
}

auto CompareTelemetryDirectoryNames(const std::string& lhs, const std::string& rhs) -> bool {
  const auto split = [](const std::string& name) -> std::pair<std::string_view, std::string_view> {
    std::size_t digits_begin = name.size();
    while (digits_begin > 0 && std::isdigit(static_cast<unsigned char>(name[digits_begin - 1])) != 0) {
      --digits_begin;
    }
    const std::string_view view{name};
    return {view.substr(0, digits_begin), view.substr(digits_begin)};
  };
  const auto [lhs_prefix, lhs_digits] = split(lhs);
  const auto [rhs_prefix, rhs_digits] = split(rhs);
  if (!lhs_digits.empty() && !rhs_digits.empty() && lhs_prefix == rhs_prefix) {
    const auto strip_leading_zeros = [](std::string_view digits) -> std::string_view {
      const auto first_significant = digits.find_first_not_of('0');
      return first_significant == std::string_view::npos ? std::string_view{} : digits.substr(first_significant);
    };
    const std::string_view lhs_significant = strip_leading_zeros(lhs_digits);
    const std::string_view rhs_significant = strip_leading_zeros(rhs_digits);
    if (lhs_significant.size() != rhs_significant.size()) {
      return lhs_significant.size() < rhs_significant.size();
    }
    return lhs_significant < rhs_significant;
  }
  return lhs < rhs;
}

auto DetectIoctlTarget(const std::filesystem::path& device_path)
    -> std::expected<std::unique_ptr<ITarget>, astl_status_code> {
  ScmiIoctlInterface ioctl_interface{device_path};
  scmi_tlm_abi_info  info{};
  auto               status = ioctl_interface.GetAbiInfo(info);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_INFO("ScmiTopologyPlugin::ScanForTargets: skipping SCMI ioctl device {}: {}", device_path.string(),
                  astl::to_string(status));
    return {};
  }

  const auto raw_uuid    = ScmiIoctlInterface::FormatDeImplementationVersion(info);
  const auto uuid_result = scmi::spec::GetNormalizedUuid(raw_uuid);
  if (!uuid_result) {
    return std::unexpected(uuid_result.error());
  }

  const auto telemetry_subdirectory =
      ScmiIoctlInterface::TelemetrySubdirectoryFromDeviceName(device_path.filename().string());
  const auto target_name = BuildTargetName(telemetry_subdirectory);
  ASTL_LOG_INFO("ScmiTopologyPlugin::ScanForTargets: Successfully detected SCMI/ioctl target with UUID {}",
                uuid_result->normalized_value);
  auto target_ptr = std::make_unique<ScmiTarget>(target_name, "Target discovered via SCMI ioctl",
                                                 telemetry_subdirectory, nullptr, uuid_result->normalized_value);
  return target_ptr;
}

auto ListSortedIoctlDevices(const std::filesystem::path& device_root)
    -> std::expected<std::vector<std::filesystem::directory_entry>, astl_status_code> {
  std::error_code ec;
  if (!std::filesystem::exists(device_root, ec)) {
    ASTL_LOG_WARNING(
        "ScmiTopologyPlugin::ScanForTargets: SCMI ioctl device root path does not exist; "
        "skipping SCMI ioctl target discovery");
    return std::vector<std::filesystem::directory_entry>{};
  }
  if (ec) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check SCMI ioctl device root path: {}", ec.message());
    return std::unexpected(ASTL_STATUS_FILE_ERROR);
  }

  std::vector<std::filesystem::directory_entry> entries;
  try {
    for (const auto& entry : std::filesystem::directory_iterator(device_root)) {
      if (ScmiIoctlInterface::IsLikelyTelemetryDeviceName(entry.path().filename().string())) {
        entries.push_back(entry);
      }
    }
  } catch (const std::filesystem::filesystem_error& error) {
    ASTL_LOG_WARNING("ScmiTopologyPlugin::ScanForTargets: Failed to list SCMI ioctl devices under {}: {}",
                     device_root.string(), error.what());
    return std::vector<std::filesystem::directory_entry>{};
  }

  std::sort(entries.begin(), entries.end(),
            [](const std::filesystem::directory_entry& lhs, const std::filesystem::directory_entry& rhs) {
              return CompareTelemetryDirectoryNames(lhs.path().filename().string(), rhs.path().filename().string());
            });
  return entries;
}

auto BuildTargetsFromIoctlDevices(const std::vector<std::filesystem::directory_entry>& ioctl_devices)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;
  for (const auto& entry : ioctl_devices) {
    auto target = DetectIoctlTarget(entry.path());
    if (!target) {
      if (target.error() == ASTL_STATUS_SUCCESS) {
        continue;
      }
      ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to detect ioctl target {}", entry.path().string());
      return std::unexpected(target.error());
    }
    if (target.value()) {
      targets.push_back(std::move(target.value()));
    }
  }
  return targets;
}

auto ScanForIoctlTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  auto sorted_devices = ListSortedIoctlDevices(configuration.scmi_ioctl_device_root_path);
  if (!sorted_devices) {
    return std::unexpected(sorted_devices.error());
  }
  return BuildTargetsFromIoctlDevices(*sorted_devices);
}

}  // namespace detail

auto ScanForTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  const auto backend_preference = GetScmiBackendPreference();
  if (ScmiPreferenceAllowsIoctl(backend_preference)) {
    auto ioctl_targets = detail::ScanForIoctlTargets(configuration);
    if (!ioctl_targets) {
      if (backend_preference == ScmiBackendPreference::IOCTL) {
        return ioctl_targets;
      }
    } else if (!ioctl_targets->empty() || backend_preference == ScmiBackendPreference::IOCTL) {
      return ioctl_targets;
    }
  }

  if (!ScmiPreferenceAllowsSysfs(backend_preference)) {
    return std::vector<std::unique_ptr<ITarget> >{};
  }

  FileInterface scmi_sysfs_file_interface{configuration.scmi_sysfs_telemetry_root_path};
  return detail::ScanForTargetsOnFileInterface(configuration, std::move(scmi_sysfs_file_interface));
}

}  // namespace astl::ScmiTopologyPlugin
