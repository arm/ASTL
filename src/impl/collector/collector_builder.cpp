// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <vector>

#include "astl_file_interface.hpp"
#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_collector.hpp"
#  include "libsensors/libsensors_target.hpp"
#endif
#include "common/scmi/scmi_constants.hpp"
#include "config/astl_configuration.hpp"
#include "target.hpp"

namespace astl {

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
  std::unordered_map<const ITarget*, std::vector<std::unique_ptr<ICollector>>> collectors;

  std::filesystem::path scmi_sysfs_root_path{configuration.scmi_sysfs_telemetry_root_path};

  using ScmiCollector = astl::ScmiSysfsCollector<astl::FileInterface>;

  for (const auto& cur_target : targets) {
    if (cur_target->GetCollectorType() == CollectorType::SCMI) {
      std::filesystem::path             scmi_target_path = scmi_sysfs_root_path / cur_target->Name();
      astl::FileInterface               scmi_target_file_interface{scmi_target_path};
      std::unique_ptr<astl::ICollector> scmi_collector =
          std::make_unique<ScmiCollector>(std::move(scmi_target_file_interface));
      std::vector<std::unique_ptr<astl::ICollector>> collectors_for_target;
      collectors_for_target.push_back(std::move(scmi_collector));
      collectors.emplace(cur_target.get(), std::move(collectors_for_target));
    } else if (cur_target->GetCollectorType() == CollectorType::LIBSENSORS) {
#if defined(ASTL_INCLUDE_LIBSENSORS)
      if (const auto* libsensors_target = dynamic_cast<const astl::LibsensorsTarget*>(cur_target.get())) {
        std::vector<std::unique_ptr<astl::ICollector>> collectors_for_target;
        collectors_for_target.push_back(std::make_unique<astl::LibsensorsCollector>(libsensors_target->ShareApi()));
        collectors.emplace(cur_target.get(), std::move(collectors_for_target));
      }
#endif
    } else {
      ASTL_LOG_ERROR("BuildCollectorManager: Unsupported collector type for target {}", cur_target->Name());
      return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
    }
  }

  return std::make_unique<CollectorManager>(std::move(collectors));
}

}  // namespace astl
