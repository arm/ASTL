// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_TOPOLOGY_PLUGIN_HPP_
#define SCMI_TOPOLOGY_PLUGIN_HPP_

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "common/scmi/scmi_constants.hpp"
#include "config/astl_configuration.hpp"
#include "target.hpp"
#include "topology/scmi_target.hpp"

namespace astl {

namespace ScmiTopologyPlugin {

namespace detail {

static auto BuildTargetName(const std::string& telemetry_subdirectory) -> std::string {
  return ScmiTarget::NameForTelemetrySubdirectory(telemetry_subdirectory);
}

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
  auto de_implementation_version_path =
      scmi_sysfs_file_interface.GetBasePath() / telemetry_subdirectory / "de_implementation_version";

  auto is_valid = scmi_sysfs_file_interface.IsValid(de_implementation_version_path);
  if (!is_valid) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check existence of de_implementation_version");
    return std::unexpected(is_valid.error());
  }
  if (!(is_valid.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not exist.  "
        "skipping target directory {}",
        telemetry_subdirectory);
    return {};
  }

  auto has_read_permission = scmi_sysfs_file_interface.HasReadPermission(de_implementation_version_path);
  if (!has_read_permission) {
    ASTL_LOG_ERROR("ScmiTopologyPlugin::ScanForTargets: Failed to check permissions of de_implementation_version");
    return std::unexpected(has_read_permission.error());
  }
  if (!(has_read_permission.value())) {
    ASTL_LOG_INFO(
        "ScmiTopologyPlugin::ScanForTargets: Info file de_implementation_version did not have read permissions.  "
        "skipping target directory {}",
        telemetry_subdirectory);
    return {};
  }

  std::string read_content;
  auto        read_status = scmi_sysfs_file_interface.Read(de_implementation_version_path, read_content);
  if (read_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR(
        "ScmiTopologyPlugin::ScanForTargets: Failed to read content of de_implementation_version "
        "for target {}",
        telemetry_subdirectory);
    return std::unexpected(read_status);
  }
  const auto uuid_result = scmi::spec::GetNormalizedUuid(read_content);
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
static auto CompareTelemetryDirectoryNames(const std::string& lhs, const std::string& rhs) -> bool {
  // Split a name into its leading non-digit prefix and its trailing run of digits.
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
    // Compare the digit runs numerically without converting to an integer, so arbitrarily large
    // suffixes are handled without overflow: drop leading zeros, then the shorter run is the smaller
    // number, and equal-length runs compare lexicographically.
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
 */
inline auto ScanForTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  FileInterface scmi_sysfs_file_interface{configuration.scmi_sysfs_telemetry_root_path};
  return detail::ScanForTargetsOnFileInterface(configuration, std::move(scmi_sysfs_file_interface));
}

}  // namespace ScmiTopologyPlugin

}  // namespace astl

#endif  // SCMI_TOPOLOGY_PLUGIN_HPP_
