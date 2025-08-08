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
#include "config/astl_configuration.hpp"
#include "target.hpp"

namespace astl {

constexpr std::string_view kDefaultScmiSysfsTelemetryRootPath = "/tmp/fuse/scmi/scmi_telemetry";

/**
 * @brief Builds collectors for the given targets based on the provided configuration.
 *
 * @param targets The list of targets for which collectors are to be built.
 * @param configuration The configuration containing parameters for collector creation.
 * @return An initialized ICollectorManager associating each target with its corresponding collectors, or an error code.
 *         Note the RegisterSampleSink() function will still need to be called on the returned collector manager
 */
auto BuildCollectorManager(const std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration)
    -> std::expected<std::unique_ptr<ICollectorManager>, astl_status_code> {
  std::unordered_map<ITarget*, std::vector<std::unique_ptr<ICollector>>> collectors;
  for (const auto& cur_target : targets) {
    /// @todo ASTL-146 Instead of hard-coding an SCMI/SysFS collector,
    ///                dynamically assign an appropriate collector for each target
    astl::FileInterface scmi_sysfs_file_interface{configuration.scmi_sysfs_telemetry_root_path
                                                      ? *configuration.scmi_sysfs_telemetry_root_path
                                                      : std::filesystem::path{kDefaultScmiSysfsTelemetryRootPath}};
    using ScmiCollector = astl::ScmiSysfsCollector<decltype(scmi_sysfs_file_interface)>;
    std::unique_ptr<astl::ICollector> scmi_collector =
        std::make_unique<ScmiCollector>(std::move(scmi_sysfs_file_interface));
    std::vector<std::unique_ptr<astl::ICollector>> collectors_for_target;
    collectors_for_target.push_back(std::move(scmi_collector));
    collectors.emplace(cur_target.get(), std::move(collectors_for_target));
  }

  return std::make_unique<CollectorManager>(std::move(collectors));
}

}  // namespace astl
