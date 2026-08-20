// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <memory>
#include <unordered_map>
#include <vector>

#include "astl_file_interface.hpp"
#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"
#include "collector/scmi_backend_selection.hpp"
#include "collector/scmi_ioctl_collector.hpp"
#include "collector/scmi_ioctl_interface.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_collector.hpp"
#  include "libsensors/libsensors_target.hpp"
#endif
#include "config/astl_configuration.hpp"
#include "target.hpp"
#if defined(ASTL_INCLUDE_PROCFS)
#  include "collector/procfs_collector.hpp"
#  include "common/procfs_utils.hpp"
#  include "topology/procfs_target.hpp"
#endif

namespace astl {
namespace {

using CollectorList          = std::vector<std::unique_ptr<ICollector>>;
using CollectorMap           = std::unordered_map<const ITarget*, CollectorList>;
using ScmiSysfsFileCollector = ScmiSysfsCollector<FileInterface>;

/**
 * @brief Wraps a single collector in the list shape consumed by CollectorManager.
 *
 * @param collector Collector to move into the list.
 * @return List containing the provided collector.
 */
auto MakeCollectorList(std::unique_ptr<ICollector> collector) -> CollectorList {
  CollectorList collectors;
  collectors.push_back(std::move(collector));
  return collectors;
}

/**
 * @brief Builds an SCMI collector using the selected backend preference.
 *
 * @param target SCMI target requiring a collector.
 * @param configuration Runtime paths and backend configuration.
 * @return Collector instance, or an ASTL error when target metadata is invalid.
 */
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

/**
 * @brief Builds the collector list for an SCMI target.
 *
 * @param target SCMI target requiring a collector.
 * @param configuration Runtime paths and backend configuration.
 * @return Collector list, or an ASTL error when the SCMI collector cannot be built.
 */
auto BuildScmiCollectors(const ITarget& target, const AstlConfiguration& configuration)
    -> std::expected<CollectorList, astl_status_code> {
  auto collector = BuildScmiCollector(target, configuration);
  if (!collector.has_value()) {
    return std::unexpected(collector.error());
  }
  return MakeCollectorList(std::move(*collector));
}

/**
 * @brief Builds the collector list for a libsensors target when libsensors support is compiled in.
 *
 * @param target Target that may contain libsensors API sharing state.
 * @return Collector list; empty when libsensors support is unavailable or target metadata is not libsensors-specific.
 */
auto BuildLibsensorsCollectors([[maybe_unused]] const ITarget& target) -> CollectorList {
  CollectorList collectors;
#if defined(ASTL_INCLUDE_LIBSENSORS)
  if (const auto* libsensors_target = dynamic_cast<const LibsensorsTarget*>(&target)) {
    collectors.push_back(std::make_unique<LibsensorsCollector>(libsensors_target->ShareApi()));
  }
#endif
  return collectors;
}

/**
 * @brief Builds the collector list for a procfs target.
 *
 * @param target Procfs target, optionally carrying an overridden procfs root path.
 * @return Collector list containing one procfs collector.
 */
#if defined(ASTL_INCLUDE_PROCFS)
auto BuildProcfsCollectors(const ITarget& target) -> CollectorList {
  auto procfs_root_path = procfs::kDefaultProcfsRootPath;
  if (const auto* procfs_target = dynamic_cast<const ProcfsTarget*>(&target)) {
    procfs_root_path = procfs_target->ProcfsRootPath();
  }

  FileInterface procfs_file_interface{procfs_root_path};
  return MakeCollectorList(std::make_unique<ProcfsCollector>(std::move(procfs_file_interface)));
}
#endif

/**
 * @brief Dispatches collector construction based on target collector type.
 *
 * @param target Target requiring collectors.
 * @param configuration Runtime paths and backend configuration.
 * @return Collector list, or an ASTL error for unsupported or invalid targets.
 */
auto BuildCollectorsForTarget(const ITarget& target, const AstlConfiguration& configuration)
    -> std::expected<CollectorList, astl_status_code> {
  switch (target.GetCollectorType()) {
    case CollectorType::SCMI:
      return BuildScmiCollectors(target, configuration);
    case CollectorType::LIBSENSORS:
      return BuildLibsensorsCollectors(target);
    case CollectorType::PROCFS:
#if defined(ASTL_INCLUDE_PROCFS)
      return BuildProcfsCollectors(target);
#else
      ASTL_LOG_ERROR("BuildCollectorManager: procfs target '{}' is unsupported by this build", target.Name());
      return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
#endif
    default:
      ASTL_LOG_ERROR("BuildCollectorManager: Unsupported collector type for target {}", target.Name());
      return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
  }
}

}  // namespace

/**
 * @brief Builds collectors for the given targets based on the provided configuration.
 *
 * @param targets The list of targets for which collectors are to be built.
 * @param configuration The configuration containing parameters for collector creation.
 * @return An initialized ICollectorManager associating each target with its corresponding collectors, or an error code.
 *         Note the RegisterRawSampleSink() function will still need to be called on the returned collector manager
 */
auto BuildCollectorManager(const std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration)
    -> std::expected<std::unique_ptr<ICollectorManager>, astl_status_code> {
  CollectorMap collectors;

  for (const auto& cur_target : targets) {
    auto collectors_for_target = BuildCollectorsForTarget(*cur_target, configuration);
    if (!collectors_for_target.has_value()) {
      return std::unexpected(collectors_for_target.error());
    }
    if (!collectors_for_target->empty()) {
      collectors.emplace(cur_target.get(), std::move(*collectors_for_target));
    }
  }

  return std::make_unique<CollectorManager>(std::move(collectors));
}

}  // namespace astl
