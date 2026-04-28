// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_TOPOLOGY_PLUGIN_HPP_
#define PROCFS_TOPOLOGY_PLUGIN_HPP_

#include <expected>
#include <memory>
#include <vector>

#include "astl_file_interface.hpp"
#include "common/procfs_utils.hpp"
#include "config/astl_configuration.hpp"
#include "topology/procfs_target.hpp"

namespace astl {

namespace ProcfsTopologyPlugin {

namespace detail {

template <typename FileInterfaceType>
auto ReadValidatedFlag(FileInterfaceType& procfs_file_interface, const std::filesystem::path& relative_path)
    -> std::expected<bool, astl_status_code> {
  const auto source_is_valid = procfs_file_interface.IsValid(relative_path);
  if (!source_is_valid.has_value()) {
    return std::unexpected(source_is_valid.error());
  }
  if (!source_is_valid.value()) {
    return false;
  }
  const auto readable = procfs_file_interface.HasReadPermission(relative_path);
  if (!readable.has_value()) {
    return std::unexpected(readable.error());
  }
  return readable.value();
}

template <typename FileInterfaceType>
auto ScanForTargetsOnFileInterface(const AstlConfiguration& configuration, FileInterfaceType procfs_file_interface)
    -> std::expected<std::vector<std::unique_ptr<ITarget>>, astl_status_code> {
  (void)configuration;
  std::vector<std::unique_ptr<ITarget>> targets;

  const auto root_is_valid = procfs_file_interface.IsValid(procfs_file_interface.GetBasePath());
  if (!root_is_valid.has_value()) {
    return std::unexpected(root_is_valid.error());
  }
  if (!root_is_valid.value()) {
    return targets;
  }

  bool has_supported_procfs_source{false};
  for (const auto& relative_path : {std::filesystem::path{"stat"}, std::filesystem::path{"meminfo"},
                                    std::filesystem::path{"loadavg"}, std::filesystem::path{"uptime"}}) {
    const auto source_is_supported = ReadValidatedFlag(procfs_file_interface, relative_path);
    if (!source_is_supported.has_value()) {
      return std::unexpected(source_is_supported.error());
    }
    if (source_is_supported.value()) {
      has_supported_procfs_source = true;
      break;
    }
  }

  if (!has_supported_procfs_source) {
    return targets;
  }

  targets.push_back(std::make_unique<ProcfsTarget>("procfs", "System telemetry discovered via procfs",
                                                   procfs_file_interface.GetBasePath()));
  return targets;
}

}  // namespace detail

inline auto ScanForTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget>>, astl_status_code> {
  FileInterface procfs_file_interface{procfs::kDefaultProcfsRootPath};
  return detail::ScanForTargetsOnFileInterface(configuration, std::move(procfs_file_interface));
}

}  // namespace ProcfsTopologyPlugin

}  // namespace astl

#endif  // PROCFS_TOPOLOGY_PLUGIN_HPP_
