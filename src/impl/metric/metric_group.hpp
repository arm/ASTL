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

#ifndef METRIC_GROUP_HPP_
#define METRIC_GROUP_HPP_

#include <string>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_errors.h"

namespace astl {

/**
 * @struct MetricGroup - represents a named group of related metrics
 * @brief Holds a collection of metrics that are logically grouped together.
 */
struct MetricGroup {
  std::string                       name;
  std::string                       description;
  std::vector<astl_metric_handle_t> metrics;

  MetricGroup(std::string name, std::string description, std::vector<astl_metric_handle_t> metrics);

  /**
   * @brief Convert this MetricGroup to an API handle.
   */
  auto ToApiHandle() const -> astl_metric_group_handle_t;

  /**
   * @brief Convert an API handle to a MetricGroup pointer.
   */
  static auto FromApiHandle(astl_metric_group_handle_t handle) -> const MetricGroup*;

  /**
   * @brief Fill in the given properties struct with this group's details.
   */
  auto ToMetricGroupProperties(astl_metric_group_properties_t* properties) const -> astl_status_code;
};

}  // namespace astl

#endif  // METRIC_GROUP_HPP_
