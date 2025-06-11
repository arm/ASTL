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

#ifndef METRIC_MANAGER_HPP_
#define METRIC_MANAGER_HPP_

#include <span>

#include "common/capabilities.hpp"
#include "i_metric_manager.hpp"

namespace astl {

class MetricManager : public IMetricManager {
 public:
  ~MetricManager() override = default;

  // MetricManager cannot be copied
  MetricManager()                                = delete;
  MetricManager(const MetricManager&)            = delete;
  MetricManager& operator=(const MetricManager&) = delete;
  MetricManager(MetricManager&&)                 = default;
  MetricManager& operator=(MetricManager&&)      = default;

  explicit MetricManager(const Capabilities& capabilities) : _capabilities(capabilities) {}

  /**
   * @brief Register the metric.
   *
   * This method is called by the orchestrator to register a new metric.
   */
  void RegisterMetric(std::unique_ptr<MetricConfig> metric_config) override;

  /**
   * @brief Get the available metrics.
   *
   * This method returns a vector of vectors of IMetric pointers.
   * This is used to retrieve all the metrics that are available for the targets.
   *
   * @return A span of IMetric pointers.
   */
  std::span<IMetric*> GetAvailableMetrics() const override;

  /**
   * @brief Get the sequence of operations needed to measure this set of metrics
   *
   * This method is called by the orchestrator to gather operations for CollectorManager to execute
   * @param metrics The sequence of metrics to be measured
   * @return either a OperationSequence, or a status code indicating the error.
   */
  std::expected<OperationSequence, astl_status_code> GetRequiredOperations(std::span<IMetric*> metrics) override;

  /**
   * @brief Process the data and route the messages to metrics.
   *
   * This method is called by the orchestrator to distribute all the samples collected.
   */
  void ProcessData(std::span<SampledData> data) override;

  /**
   * @brief Summarize the metric.
   * Depending on the metric type, this may mean aggregating samples, calculating averages, etc.
   * This is called once all the samples are processed.
   */
  void SummarizeMetrics() override;

 private:
  std::vector<std::unique_ptr<IMetric>> _metrics;
  Capabilities                          _capabilities;
};

}  // namespace astl

#endif  // METRIC_MANAGER_HPP_