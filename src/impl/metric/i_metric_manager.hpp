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

#ifndef I_METRIC_MANAGER_HPP_
#define I_METRIC_MANAGER_HPP_

#include <memory>
#include <span>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "common/i_sample_sink.hpp"
#include "common/operation.hpp"
#include "i_metric.hpp"
#include "metric_config.hpp"

namespace astl {

/**
 * @brief Interface for the Metric Manager.
 *
 * This interface defines the methods that a Metric Manager implements.
 * It is used to manage metrics in the ASTL framework.
 */
class IMetricManager {
 public:
  virtual ~IMetricManager() = default;

  IMetricManager()                                 = default;
  IMetricManager(const IMetricManager&)            = default;
  IMetricManager& operator=(const IMetricManager&) = default;
  IMetricManager(IMetricManager&&)                 = default;
  IMetricManager& operator=(IMetricManager&&)      = default;

  /**
   * @brief Register the metric.
   *
   * This method is called by the orchestrator to register a new metric.
   */
  virtual astl_status_code RegisterMetric(std::unique_ptr<MetricConfig> metric_config) = 0;

  /**
   * @brief Get the available metrics.
   *
   * This method returns a vector of vectors of IMetric pointers.
   * This is used to retrieve all the metrics that are available for the targets.
   *
   * @return A span of IMetric pointers.
   */
  virtual std::expected<std::span<IMetric*>, astl_status_code> GetAvailableMetrics() const = 0;

  /**
   * @brief Initialize the metrics.
   *
   * This method is called by the orchestrator to initialize metrics for a given target.
   */
  virtual std::expected<OperationSequence, astl_status_code> GetRequiredOperations(std::span<IMetric*> metrics) = 0;

  /**
   * @brief Process the data and route the messages to metrics.
   *
   * This method is called by the orchestrator to distribute all the samples collected.
   */
  virtual astl_status_code ProcessData(std::span<SampledData> data) = 0;

  /**
   * @brief Summarize the metrics messages.
   *
   * This method should be called to create a summary for all the metrics.
   */
  virtual astl_status_code SummarizeMetrics() = 0;
};

}  // namespace astl

#endif  // I_METRIC_MANAGER_HPP_
