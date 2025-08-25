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

#ifndef I_TOPOLOGY_PLUGIN_HPP_
#define I_TOPOLOGY_PLUGIN_HPP_

#include <expected>
#include <memory>
#include <vector>

#include "astl/astl_errors.h"
#include "target.hpp"

namespace astl {

/**
 * @brief Interface definition for a topology manager plugin.
 * Each topology plugin is meant to be very specific to a certain type of interface
 * such as scmi, hwmon, etc.
 */
struct ITopologyPlugin {
  virtual ~ITopologyPlugin()                         = default;
  ITopologyPlugin()                                  = default;
  ITopologyPlugin(const ITopologyPlugin&)            = default;
  ITopologyPlugin& operator=(const ITopologyPlugin&) = default;
  ITopologyPlugin(ITopologyPlugin&&)                 = default;
  ITopologyPlugin& operator=(ITopologyPlugin&&)      = default;

  /**
   * @brief Generate a list of targets available on this platform.
   * If this plugin is not supported on the system, an empty vector is returned.
   */
  virtual auto ScanForTargets() -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> = 0;
};

}  // namespace astl

#endif  // I_TOPOLOGY_PLUGIN_HPP_
