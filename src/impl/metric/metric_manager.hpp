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
/*******************************************************************************
 * @file metric_manager.hpp
 * @brief Defines MetricManager, which handles registration of metrics,
 *        exposes available metrics, and constructs SCMI operation sequences.
 ******************************************************************************/

#ifndef METRIC_MANAGER_HPP_
#define METRIC_MANAGER_HPP_

#include <span>
#include <unordered_map>
#include <vector>

#include "astl/astl_errors.h"
#include "collector/collection_operations.hpp"
#include "common/capabilities.hpp"
#include "i_metric_manager.hpp"

namespace astl {

class MetricManagerTestAccessor;
/**
 * @class MetricManager
 * @brief Implements IMetricManager to manage metrics and generate collection operations.
 */
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
   * @brief Register a new metric configuration.
   * @param metric_config Unique pointer to the MetricConfig to register.
   * @return ASTL_STATUS_SUCCESS on success, or an error code (e.g., ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE).
   */
  astl_status_code RegisterMetric(std::unique_ptr<MetricConfig> metric_config) override;

  /**
   * @brief Retrieve all registered metrics.
   * @return expected containing a span of registered IMetric pointers on success,
   *         or an error code if retrieval fails.
   */
  std::expected<std::span<IMetric* const>, astl_status_code> GetAvailableMetrics() const override;

  /**
   * @brief Build the sequence of operations required to collect the given metrics.
   * @param metrics Span of metric pointers for which to generate operations.
   * @return expected containing an CollectionOperations group of SCMI read operations on success,
   *         or an error code (e.g., BAD_ARGUMENT, UNSUPPORTED_COLLECTOR_TYPE).
   */
  std::expected<CollectionOperations, astl_status_code> GetRequiredOperations(
      std::span<IMetric* const> metrics) override;

  /**
   * @brief Distribute collected sample data to registered metrics.
   * @param data Span of SampledData to process.
   * @return ASTL_STATUS_SUCCESS or an appropriate error code.
   */
  astl_status_code ProcessData(std::span<SampledData> data) override;

  /**
   * @brief Finalize and summarize metrics after data processing.
   * @return ASTL_STATUS_SUCCESS or an appropriate error code.
   */
  astl_status_code SummarizeMetrics() override;

  /**
   * TODO (https://jira.arm.com/browse/ASTL-112) : remove this friend declaration
   *  once a way to inject Mock Metric to Metric Manager for testing is introduced..
   */
  friend class MetricManagerTestAccessor;

 private:
  /**
   * @brief Check if a collector type is supported by this manager.
   * @param required_collector_type The collector type to verify support for.
   * @return true if supported, false otherwise.
   */
  bool IsCollectorTypeSupported(CollectorType required_collector_type) const;

  /**
   * @brief Helper function to register a metric instance with the manager.
   * @param metric Unique pointer to the metric instance to register.
   * @param metric_config Unique pointer to the metric configuration.
   */
  void         AddMetricToMaps(std::unique_ptr<IMetric> metric, std::unique_ptr<MetricConfig> metric_config);
  Capabilities _capabilities;
  std::unordered_map<IMetric*, std::unique_ptr<IMetric>> _metrics_map;  ///< Maps metric pointers to their instances
  std::unordered_map<IMetric*, std::unique_ptr<MetricConfig>>
      _config_map;  ///< Maps metric pointers to their configurations
  std::unordered_map<uint32_t, IMetric*>
                        _operation_to_metric_map;  ///< Maps operation IDs to their corresponding metrics
  std::vector<IMetric*> _metric_handles;
};

}  // namespace astl

#endif  // METRIC_MANAGER_HPP_
