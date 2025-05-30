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

#ifndef METRIC_CONFIG_HPP_
#define METRIC_CONFIG_HPP_

#include <string>

#include "astl/astl.h"

namespace astl {

/* @brief Interface for metric configuration.
 *
 * This class defines the interface that all metric configuration types must implement.
 */
class MetricConfig {
 public:
  /**
   * @brief allow destroying metric config instances by base class pointer
   */
  virtual ~MetricConfig() = default;

  MetricConfig()                                = default;
  MetricConfig(const MetricConfig &)            = default;
  MetricConfig &operator=(const MetricConfig &) = default;
  MetricConfig(MetricConfig &&)                 = default;
  MetricConfig &operator=(MetricConfig &&)      = default;

  /**
   * @brief Return the name of the metric.
   *
   * This is a placeholder interface method that must be overridden by concrete metric config classes.
   * @return std::string The metric name.
   */
  virtual std::string Name() const = 0;
};

}  // namespace astl

#endif  // METRIC_CONFIG_HPP_