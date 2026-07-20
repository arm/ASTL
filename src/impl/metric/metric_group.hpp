// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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
  auto ToMetricGroupProperties(astl_metric_group_props_t* properties) const -> astl_status_code;
};

}  // namespace astl

#endif  // METRIC_GROUP_HPP_
