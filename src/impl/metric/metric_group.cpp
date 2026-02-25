
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

#include "metric_group.hpp"

#include "common/string_pool.hpp"

namespace astl {

MetricGroup::MetricGroup(std::string name, std::string description, std::vector<astl_metric_handle_t> metrics)
    : name(std::move(name)), description(std::move(description)), metrics(std::move(metrics)) {}

auto MetricGroup::ToApiHandle() const -> astl_metric_group_handle_t {
  return static_cast<astl_metric_group_handle_t>(this);
}

auto MetricGroup::FromApiHandle(astl_metric_group_handle_t handle) -> const MetricGroup* {
  return static_cast<const MetricGroup*>(handle);
}

/**
 * @brief Fill in the given properties struct with this group's details.
 */
auto MetricGroup::ToMetricGroupProperties(astl_metric_group_properties_t* properties) const -> astl_status_code {
  if (!properties) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  properties->_size         = sizeof(astl_metric_group_properties_t);
  properties->_handle       = ToApiHandle();
  properties->_name         = GetInternedString(name);
  properties->_description  = GetInternedString(description);
  properties->_metric_count = static_cast<uint32_t>(metrics.size());
  properties->_metrics      = nullptr;  // user can subsequently allocate and then fill via astlGetMetricGroupMetrics
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl