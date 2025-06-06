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
#include "capabilities.hpp"

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
  /**
   * @brief Default constructor is deleted to ensure that MetricConfig cannot be instantiated directly.
   */
  MetricConfig() = delete;
  /**
   * @brief Construct a MetricConfig with the given parameters.
   */

  explicit MetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                        astl_value_type_t value_type, astl_metric_type_t metric_type, const Capabilities &required_caps)
      : _metric_name(name),
        _description(description),
        _units(units),
        _value_type(value_type),
        _metric_type(metric_type),
        _required_caps(required_caps) {}
  MetricConfig(const MetricConfig &)            = default;
  MetricConfig &operator=(const MetricConfig &) = default;
  MetricConfig(MetricConfig &&)                 = default;
  MetricConfig &operator=(MetricConfig &&)      = default;

  /**
   * @brief Return the name of the metric.
   *
   * @return std::string The metric name.
   */
  const std::string &Name() const { return _metric_name; }
  /**
   * @brief Return the description of the metric.
   *
   * @return std::string The metric description.
   */
  const std::string &Description() const { return _description; }
  /**
   * @brief Return the units of the metric.
   *
   * @return astl_units_t The metric units.
   */
  astl_units_t Units() const { return _units; }
  /**
   * @brief Return the value type of the metric.
   *
   * @return astl_value_type_t The metric value type.
   */
  astl_value_type_t ValueType() const { return _value_type; }
  /**
   * @brief Return the metric type.
   *
   * @return astl_metric_type_t The metric type.
   */
  astl_metric_type_t MetricType() const { return _metric_type; }
  /**
   * @brief Return the required capabilities of the metric.
   */
  const Capabilities &GetRequiredCapabilities() const { return _required_caps; }
  /**
   * @brief Set the required capabilities of the metric.
   *
   * @param required_caps The required capabilities to set.
   */
  void SetRequiredCapabilities(const Capabilities &required_caps) { _required_caps = required_caps; }

 private:
  std::string       _metric_name;  // Metric name as specified in the configuration file
  std::string       _description;  // Human-readable description of the metric
  astl_units_t      _units;        // Measurement units for the metric (e.g., seconds, bytes)
  astl_value_type_t _value_type;   // Data type of the metric value (e.g., raw, processed)
  astl_metric_type_t
               _metric_type;    // Semantic type of the metric defined by ASTL design doc(e.g., value, delta, residency)
  Capabilities _required_caps;  // Required system capabilities or collector types to support this metric
};

}  // namespace astl

#endif  // METRIC_CONFIG_HPP_