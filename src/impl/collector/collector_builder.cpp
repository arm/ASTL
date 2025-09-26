/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include <memory>
#include <vector>

#include "astl_file_interface.hpp"
#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"
#include "collector/scmi_sysfs_collector.hpp"
#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "collector/libsensors_collector.hpp"
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

  std::filesystem::path scmi_sysfs_root_path =
      configuration.scmi_sysfs_telemetry_root_path.value_or(std::filesystem::path{kDefaultScmiSysfsTelemetryRootPath});

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
      std::vector<std::unique_ptr<astl::ICollector>> collectors_for_target;
      collectors_for_target.push_back(std::make_unique<astl::LibsensorsCollector>());
      collectors.emplace(cur_target.get(), std::move(collectors_for_target));
#endif
    } else {
      ASTL_LOG_ERROR("BuildCollectorManager: Unsupported collector type for target {}", cur_target->Name());
      return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
    }
  }

  return std::make_unique<CollectorManager>(std::move(collectors));
}

}  // namespace astl
