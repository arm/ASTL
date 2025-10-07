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

#include <expected>
#include <memory>

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "target.hpp"
#include "topology/i_topology_manager.hpp"
#include "topology/libsensors_topology_plugin.hpp"
#include "topology/scmi_topology_plugin.hpp"
#include "topology/topology_manager.hpp"

namespace astl {

// Helper function which runs a single topology plugin and does some error handling
// 2nd parameter is a function pointer to the ScanForTargets() member of a given plugin
auto ActivatePlugin(std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration,
                    std::expected<std::vector<std::unique_ptr<ITarget>>, astl_status_code> (*scanFunc)(
                        const AstlConfiguration&)) -> void {
  auto targets_detected_from_this_plugin = scanFunc(configuration);
  if (!targets_detected_from_this_plugin) {
    throw targets_detected_from_this_plugin.error();
  }

  auto validated_new_targets = std::move(targets_detected_from_this_plugin.value());

  // std::vector::append_range() would be better,
  // but g++ 13.1.0 doesn't seem to implement __cpp_lib_containers_ even when using -std=c++23
  targets.reserve(targets.size() + validated_new_targets.size());
  for (size_t ix = 0; ix < validated_new_targets.size(); ix++) {
    targets.push_back(std::move(validated_new_targets[ix]));
  }
}

auto BuildTopologyManager(const AstlConfiguration& configuration)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code> {
  std::vector<std::unique_ptr<ITarget>> targets;

  try {
    // Add more topology plugins here by calling ActivatePlugin on each
    ActivatePlugin(targets, configuration, ScmiTopologyPlugin::ScanForTargets);
    ActivatePlugin(targets, configuration, LibsensorsTopologyPlugin::ScanForTargets);
  } catch (astl_status_code& error_code) {
    return std::unexpected(error_code);
  }

  return std::make_unique<TopologyManager>(std::move(targets));
}

}  // namespace astl
