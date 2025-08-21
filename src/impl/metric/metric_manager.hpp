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
#include "astl/astl_telemetry.h"
#include "collector/collection_operations.hpp"
#include "common/capabilities.hpp"
#include "metric/i_metric.hpp"
#include "metric/i_metric_manager.hpp"

namespace astl {

class MetricManagerTestAccessor;

/**
 * @struct MetricHandle - internal representation of a astl_metric_handle_t
 * @brief Holds details of a single metric including its configuration and associated targets.
 */
struct MetricHandle {
  std::unique_ptr<MetricConfig>                                config;  //< Configuration that generated this metric
  std::unordered_map<const ITarget*, std::unique_ptr<IMetric>> target_to_metric_map;
};

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
   * @param targets       Collection of targets where this metric can be sampled
   *
   * @return ASTL_STATUS_SUCCESS on success, or an error code (e.g., ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE).
   */
  astl_status_code RegisterMetric(std::unique_ptr<MetricConfig>      metric_config,
                                  std::vector<const ITarget*> const& targets) override;

  /**
   * @brief Retrieve all registered metrics.
   * @return expected containing a span of registered astl_metric_handle_t on success,
   *         or an error code if retrieval fails.
   */
  auto GetAvailableMetrics() const -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> override;

  /**
   * @brief Retrieve all registered metrics.
   * @param target The target from which to retrieve associated metrics
   * @return expected containing a span of registered astl_metric_handle_t on success,
   *         or an error code if retrieval fails.
   */
  auto GetAvailableMetrics(const ITarget* target) const
      -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> override;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  auto GetProperties(astl_metric_handle_t      metric,
                     astl_metric_properties_t* properties) const -> astl_status_code override;

  /**
   * @brief Build the sequence of operations required to collect the given metrics.
   * @param metrics Span of metric api handles for which to generate operations.
   * @param target The target for which to generate operations.
   * @return expected containing an CollectionOperations group of SCMI read operations on success,
   *         or an error code (e.g., BAD_ARGUMENT, UNSUPPORTED_COLLECTOR_TYPE).
   */
  auto GetRequiredOperations(std::span<const astl_metric_handle_t> metrics,
                             const ITarget* target) -> std::expected<CollectionOperations, astl_status_code> override;

  /**
   * @brief Distribute collected sample data to registered metrics.
   * @param data Span of SampledData to process.
   * @return ASTL_STATUS_SUCCESS or an appropriate error code.
   */
  astl_status_code ProcessData(std::span<SampledData> data) override;

  /**
   * @brief Retrieve the collected samples for the given target and metric,
   *        or an error if the target+metric combination isn't valid
   */
  auto GetSamples(astl_metric_handle_t metric_handle, const ITarget* target)
      -> std::expected<std::span<const astl::SampledData>, astl_status_code> override;

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

  Capabilities _capabilities;

  // an API astl_metric_handle_t can represent one metric runnable on multiple targets
  // our internal representation of astl_metric_handle is `MetricHandle*`
  std::vector<std::unique_ptr<MetricHandle>> _metric_handles;

  // This is same data as in `_metric_handles`, but using raw pointers to MetricHandles for exposure
  // to the API through std::span<astl_metric_handle_t>
  // We can't directly cast a vector<std::unique_ptr<MetricHandle>>
  // to std::span<astl_metric_handle_t> like we need in GetAvailableMetrics.
  std::vector<astl_metric_handle_t> _metric_api_handles;

  // each Target supports multiple different metrics
  std::unordered_map<const ITarget*, std::vector<astl_metric_handle_t>> _target_to_metrics_map;

  // Maps operation IDs to their corresponding metrics
  std::unordered_map<uint32_t, IMetric*> _operation_to_metric_map;
};

}  // namespace astl

#endif  // METRIC_MANAGER_HPP_
