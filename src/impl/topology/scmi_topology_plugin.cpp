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

#include "topology/scmi_topology_plugin.hpp"

#include <expected>
#include <memory>
#include <vector>

#include "astl/astl_errors.h"
#include "target.hpp"

namespace astl {

auto ScmiTopologyPlugin::ScanForTargets() -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;
  // @todo ASTL-144 This is a fake target placeholder
  targets.push_back(std::make_unique<Target>("AP0", "The SCMI interface on Socket0", CollectorType::SCMI));
  return targets;
}

}  // namespace astl
