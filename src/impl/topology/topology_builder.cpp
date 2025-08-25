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
#include "target.hpp"
#include "topology/i_topology_manager.hpp"
#include "topology/i_topology_plugin.hpp"
#include "topology/scmi_topology_plugin.hpp"
#include "topology/topology_manager.hpp"

namespace astl {

auto BuildTopologyManager() -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;

  // If you add more topology plugins, construct an instance and add them to this vector
  std::vector<std::unique_ptr<ITopologyPlugin> > topology_plugins;
  topology_plugins.push_back(std::make_unique<ScmiTopologyPlugin>());

  for (auto& cur_plugin : topology_plugins) {
    auto targets_detected_from_this_plugin = cur_plugin->ScanForTargets();
    if (!targets_detected_from_this_plugin) {
      return std::unexpected(targets_detected_from_this_plugin.error());
    }

    auto validated_new_targets = std::move(targets_detected_from_this_plugin.value());

    // std::vector::append_range() would be better,
    // but g++ 13.1.0 doesn't seem to implement __cpp_lib_containers_ even when using -std=c++23
    targets.reserve(targets.size() + validated_new_targets.size());
    for (size_t ix = 0; ix < validated_new_targets.size(); ix++) {
      targets.push_back(std::move(validated_new_targets[ix]));
    }
  }

  return std::make_unique<TopologyManager>(std::move(targets));
}

}  // namespace astl
