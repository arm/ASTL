// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_TOPOLOGY_PLUGIN_HPP_
#define SCMI_TOPOLOGY_PLUGIN_HPP_

#include <algorithm>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"
#include "target.hpp"

namespace astl {

namespace ScmiTopologyPlugin {

namespace detail {

auto BuildTargetName(const std::string& telemetry_subdirectory) -> std::string;

auto BuildSysfsTargetFromImplementationVersion(const std::string& implementation_version,
                                               const std::string& telemetry_subdirectory)
    -> std::expected<std::unique_ptr<ITarget>, astl_status_code>;

/**
 * @brief Returns a Target (e.g. named "scmi_tlm-0") accessible via SCMI from the given subdirectory scmi_telemetry
 *
 * @param scmi_sysfs_file_interface a FileInterface implementation (or mock) to use to explore the SCMI targets
 * @param telemetry_subdirectory A string representing the subdirectory under which to look for SCMI targets (e.g.
 * "tlm-0")
 */
template <typename FileInterfaceType>
auto DetectTarget(FileInterfaceType& scmi_sysfs_file_interface, std::string const& telemetry_subdirectory)
    -> std::expected<std::unique_ptr<ITarget>, astl_status_code> {
  std::expected<std::unique_ptr<ITarget>, astl_status_code> target;
  auto                                                      de_implementation_version_path =
      scmi_sysfs_file_interface.GetBasePath() / telemetry_subdirectory / "de_implementation_version";

  auto is_valid = scmi_sysfs_file_interface.IsValid(de_implementation_version_path);
  if (!is_valid) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check existence of de_implementation_version");
    target = std::unexpected(is_valid.error());
  } else if (!(is_valid.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not exist.  "
        "skipping target directory {}",
        telemetry_subdirectory);
  } else {
    auto has_read_permission = scmi_sysfs_file_interface.HasReadPermission(de_implementation_version_path);
    if (!has_read_permission) {
      ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check permissions of de_implementation_version");
      target = std::unexpected(has_read_permission.error());
    } else if (!(has_read_permission.value())) {
      ASTL_LOG_INFO(
          "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not have read permissions.  "
          "skipping target directory {}",
          telemetry_subdirectory);
    } else {
      std::string read_content;
      auto        read_status = scmi_sysfs_file_interface.Read(de_implementation_version_path, read_content);
      if (read_status != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR(
            "ScmiTopologyPlugin::ScanForTargets: Failed to read content of de_implementation_version "
            "for target {}",
            telemetry_subdirectory);
        target = std::unexpected(read_status);
      } else {
        target = BuildSysfsTargetFromImplementationVersion(read_content, telemetry_subdirectory);
      }
    }
  }

  return target;
}

/**
 * @brief Natural-order comparison for telemetry target directory names.
 *
 * Plain lexicographic ordering mis-sorts multi-digit indices (e.g. `tlm-10` would sort before
 * `tlm-2`), which would shift each target's position and therefore the derived
 * `global_instance = target_index * count + local_instance` numbering. This splits each name into a
 * leading non-numeric prefix and an optional trailing decimal suffix; when both names share the same
 * prefix and end in digits the numeric suffixes are compared, otherwise it falls back to a plain
 * string comparison for non `tlm-N` directory names.
 */
auto CompareTelemetryDirectoryNames(const std::string& lhs, const std::string& rhs) -> bool;

/**
 * @brief Validate the SCMI telemetry root and return its immediate subdirectories in natural order.
 *
 * Returns an empty list (success) when the telemetry root does not exist or cannot be listed, and an
 * error only when the root path itself cannot be validated.
 */
template <typename FileInterfaceType>
auto ListSortedTelemetryDirectories(FileInterfaceType& scmi_sysfs_file_interface)
    -> std::expected<std::vector<std::filesystem::directory_entry>, astl_status_code> {
  const auto telemetry_root_is_valid = scmi_sysfs_file_interface.IsValid(scmi_sysfs_file_interface.GetBasePath());
  if (!telemetry_root_is_valid) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check SCMI sysfs telemetry root path");
    return std::unexpected(telemetry_root_is_valid.error());
  }
  if (!*telemetry_root_is_valid) {
    ASTL_LOG_WARNING(
        "ScmiTopologyPlugin::ScanForTargets: SCMI sysfs telemetry root path does not exist; "
        "skipping SCMI target discovery");
    return std::vector<std::filesystem::directory_entry>{};
  }
  auto telemetry_root_children = scmi_sysfs_file_interface.GetSubdirectories();
  if (!telemetry_root_children) {
    ASTL_LOG_WARNING("ScmiTopologyPlugin::ScanForTargets: Failed to list children of SCMI sysfs telemetry root");
    return std::vector<std::filesystem::directory_entry>{};
  }

  // Sort children by filename so multi-instance SCMI targets (e.g. tlm-0, tlm-1, ...) are
  // discovered in a deterministic order. Downstream metric generation uses each target's position
  // as the per-target instance offset (global_instance = target_index * count + local_instance),
  // so this ordering is what guarantees e.g. PSS.0..2 land on tlm-0 and PSS.3..5 land on tlm-1.
  // A natural-order comparison is used so multi-digit indices (e.g. tlm-10) don't sort before
  // lower-numbered targets (e.g. tlm-2) and shift the offsets.
  std::sort(telemetry_root_children->begin(), telemetry_root_children->end(),
            [](const std::filesystem::directory_entry& lhs, const std::filesystem::directory_entry& rhs) {
              return CompareTelemetryDirectoryNames(lhs.path().filename().string(), rhs.path().filename().string());
            });
  return std::move(telemetry_root_children.value());
}

/**
 * @brief Detect and collect SCMI targets from the given (already ordered) telemetry directories.
 */
template <typename FileInterfaceType>
auto BuildTargetsFromDirectories(FileInterfaceType&                                   scmi_sysfs_file_interface,
                                 const std::vector<std::filesystem::directory_entry>& telemetry_directories)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;
  for (const auto& entry : telemetry_directories) {
    auto target = DetectTarget(scmi_sysfs_file_interface, entry.path().filename().string());
    if (!target) {
      if (target.error() == ASTL_STATUS_SUCCESS) {
        // Not an error, just no target found in this directory
        continue;
      }
      ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to detect target in directory {}",
                     entry.path().string());
      return std::unexpected(target.error());
    }
    if (target.value()) {
      targets.push_back(std::move(target.value()));
    }
  }
  return targets;
}

auto DetectIoctlTarget(const std::filesystem::path& device_path)
    -> std::expected<std::unique_ptr<ITarget>, astl_status_code>;

auto ListSortedIoctlDevices(const std::filesystem::path& device_root)
    -> std::expected<std::vector<std::filesystem::directory_entry>, astl_status_code>;

auto BuildTargetsFromIoctlDevices(const std::vector<std::filesystem::directory_entry>& ioctl_devices)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code>;

auto ScanForIoctlTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code>;

/**
 * @brief Returns a list of targets accessible via SCMI on this platform
 *
 * @param configuration The ASTL configuration containing SCMI sysfs path overrides
 * @param scmi_sysfs_file_interface A FileInterface (or mock) to use for exploring the SCMI targets
 */
template <typename FileInterfaceType>
auto ScanForTargetsOnFileInterface(const AstlConfiguration& configuration, FileInterfaceType scmi_sysfs_file_interface)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  (void)configuration;  // currently unused
  auto sorted_directories = ListSortedTelemetryDirectories(scmi_sysfs_file_interface);
  if (!sorted_directories) {
    return std::unexpected(sorted_directories.error());
  }
  return BuildTargetsFromDirectories(scmi_sysfs_file_interface, *sorted_directories);
}

}  // namespace detail

/**
 * @brief Returns a list of targets accessible via SCMI on this platform
 *
 * In automatic mode, ioctl targets are preferred when any are usable. If ioctl
 * discovery finds no targets, discovery falls back to the legacy sysfs backend.
 * Forced ioctl or forced sysfs mode disables the other backend.
 *
 * @param configuration ASTL configuration containing SCMI sysfs and ioctl path overrides.
 * @return Discovered SCMI targets, or an ASTL status on discovery failure.
 */
auto ScanForTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code>;

}  // namespace ScmiTopologyPlugin

}  // namespace astl

#endif  // SCMI_TOPOLOGY_PLUGIN_HPP_
