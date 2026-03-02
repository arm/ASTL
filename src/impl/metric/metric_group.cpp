// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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
