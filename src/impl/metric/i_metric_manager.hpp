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
#include "collector/collection_operations.hpp"
#include "common/astl_defines.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "common/metric_config.hpp"
#include "operation/operation.hpp"
#include "serdes/protobuf_serdes.hpp"

namespace astl {

/**
 * @brief Interface for the Metric Manager.
 *
 * This interface defines the methods that a Metric Manager implements.
 * It is used to manage metrics in the ASTL framework.
 */
struct IMetricManager {
  static constexpr bool kSerializable{true};
  virtual ~IMetricManager() = default;

  IMetricManager()                                 = default;
  IMetricManager(const IMetricManager&)            = default;
  IMetricManager& operator=(const IMetricManager&) = default;
  IMetricManager(IMetricManager&&)                 = default;
  IMetricManager& operator=(IMetricManager&&)      = default;

  /**
   * @brief Helper to look up a ICounter handle representing a counter for a specific target from a counter API handle
   *
   * @param counter_handle an astl_counter_handle_t representing potentially many identical counters that
   *        differ only in their target.
   * @param target the target on which to get the counter instance.
   * @return the ICounter instance representing the given counter_handle on the given target. (or an error)
   */
  [[nodiscard]] virtual auto GetCounterOnTarget(astl_counter_handle_t counter_handle, const ITarget* target) const
      -> std::expected<IMetric*, astl_status_code> = 0;

  /**
   * @brief Register the counter.
   *
   * This method is called by the orchestrator to register a new counter.
   */
  [[nodiscard]] virtual auto RegisterCounter(std::unique_ptr<MetricConfig>      counter_config,
                                             std::vector<const ITarget*> const& targets) -> astl_status_code = 0;

  /**
   * @brief Get the number of available counters for the given target.
   *
   * This method returns a count of astl_counter_handle_t api handles.
   *
   * @param target The target from which to retrieve associated counters
   *
   * @return The number of available counters for the given target, or an error.
   */
  virtual auto GetNumAvailableCounters(const ITarget* target) const -> size_t = 0;

  /**
   * @brief Get the available counters.
   *
   * This method returns a span of astl_counter_handle_t api handles.
   * This is used to retrieve all the counters that are available for the given target.
   *
   * @param target The target from which to retrieve associated counters
   *
   * @return A span<astl_counter_handle_t> containing all registered counters, or an error.
   */
  [[nodiscard]] virtual auto GetAvailableCounters(const ITarget* target) const
      -> std::expected<std::span<const astl_counter_handle_t>, astl_status_code> = 0;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   *
   * @param counter The counter API handle for potentially many identical counters that differ only in their target
   * @param properties A non-null pointer to a struct containing properties to fill in, comes from astlGetCounters API
   *
   * @return An astl_status_code indicating success or ASTL_STATUS_BAD_PARAM
   */
  [[nodiscard]] virtual auto GetCounterProperties(astl_counter_handle_t      counter,
                                                  astl_counter_properties_t* properties) const -> astl_status_code = 0;

  /**
   * @brief Get the collection of collector operations needed to sample the given counter on the given target
   *
   * This method is called by the orchestrator to retrieve operations to send to CollectorManager
   *
   * @param counters A collection of counter API handles to collect
   * @param target A pointer to a target on which to collect samples for the given counters
   *
   * @return A CollectionOperations struct with operations for the CollectorManager to execute
   *         OR a status code indicating the nature of an error
   */
  [[nodiscard]] virtual auto GetCounterRequiredOperations(std::span<const astl_counter_handle_t> counters,
                                                          const ITarget*                         target)
      -> std::expected<CollectionOperations, astl_status_code> = 0;

  /**
   * @brief Helper to look up a IMetric handle for a specific target from a metric API handle
   */
  [[nodiscard]] virtual auto GetMetricOnTarget(astl_metric_handle_t metric_handle, const ITarget* target) const
      -> std::expected<IMetric*, astl_status_code> = 0;

  /**
   * @brief Register a sink to receive processed samples produced by metrics.
   *
   * Multiple sinks may be registered. Typical sinks include the output manager or test harnesses.
   *
   * @param sink Non-null pointer whose lifetime must exceed the period of registration.
   * @return ASTL_STATUS_SUCCESS on success, ASTL_STATUS_BAD_ARGUMENT if sink is null.
   */
  [[nodiscard]] virtual auto RegisterProcessedSampleSink(IProcessedSampleSink* sink) -> astl_status_code = 0;

  /**
   * @brief Remove a previously registered processed sample sink.
   *
   * A no-op if the sink was not registered.
   *
   * @param sink Pointer passed during registration.
   * @return ASTL_STATUS_SUCCESS (even if not found), ASTL_STATUS_BAD_ARGUMENT if sink is null.
   */
  [[nodiscard]] virtual auto UnregisterProcessedSampleSink(IProcessedSampleSink* sink) -> astl_status_code = 0;

  /**
   * @brief Register the metric.
   *
   * This method is called by the orchestrator to register a new metric.
   */
  [[nodiscard]] virtual auto RegisterMetric(std::unique_ptr<MetricConfig>      metric_config,
                                            std::vector<const ITarget*> const& targets) -> astl_status_code = 0;

  /**
   * @brief Get the number of available metrics for the given target.
   *
   * This method returns a count of astl_metric_handle_t api handles.
   *
   * @param target The target from which to retrieve associated metrics
   *
   * @return The number of available metrics for the given target, or an error.
   */
  virtual auto GetNumAvailableMetrics(const ITarget* target) const -> size_t = 0;

  /**
   * @brief Get the available metrics.
   *
   * This method returns a span of astl_metric_handle_t api handles.
   * This is used to retrieve all the metrics that are available for the given target.
   *
   * @param target The target from which to retrieve associated metrics
   *
   * @return A span<astl_metric_handle_t> containing all registered metrics, or an error.
   */
  [[nodiscard]] virtual auto GetAvailableMetrics(const ITarget* target) const
      -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> = 0;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   *
   * @param metric The metric API handle for potentially many identical metrics that differ only in their target
   * @param properties A non-null pointer to a struct containing that GetProperties will fill in
   *
   * @return An astl_status_code indicating success or ASTL_STATUS_BAD_PARAM
   */
  [[nodiscard]] virtual auto GetProperties(astl_metric_handle_t metric, astl_metric_properties_t* properties) const
      -> astl_status_code = 0;

  /**
   * @brief Get the collection of collector operations needed to sample the given metric on the given target
   *
   * This method is called by the orchestrator to retrieve operations to send to CollectorManager
   *
   * @param metrics A collection of metric API handles to collect
   * @param target A poitner to a target on which to collect samples for the given metrics
   *
   * @return A CollectionOperations struct with operations for the CollectorManager to execute
   *         OR a status code indicating the nature of an error
   */
  [[nodiscard]] virtual auto GetRequiredOperations(std::span<const astl_metric_handle_t> metrics, const ITarget* target)
      -> std::expected<CollectionOperations, astl_status_code> = 0;

  /**
   * @brief Process the data and route the messages to metrics.
   *
   * This method is called by the orchestrator to distribute all the samples collected.
   *
   * @param data A collection of raw sampled data points for the metrics to process
   */
  [[nodiscard]] virtual auto ProcessRawSamples(RawSamplesMap& raw_samples) -> astl_status_code = 0;

  /**
   * @brief Accept processed samples from a metric implementation.
   *
   * This allows the metric manager (or another upstream component) to act as an aggregator
   * and forward samples to registered sinks.
   *
   * @param metric Producing metric instance.
   * @param processed_samples Span of processed samples, valid only during the call.
   * @return ASTL_STATUS_SUCCESS or an error code from dispatching to sinks.
   */
  [[nodiscard]] virtual auto SinkProcessedSamples(const IMetric*                        metric,
                                                  std::span<const ProcessedSampledData> processed_samples)
      -> astl_status_code = 0;

  /**
   * @brief Return the metric groups registered in the manager.
   *
   * @return A span<astl_metric_group_handle_t> containing all registered metric groups, or an error.
   */
  [[nodiscard]] virtual auto GetMetricGroups() const -> std::span<const astl_metric_group_handle_t> = 0;

  /**
   * @brief Return the metric groups registered in the manager for a specific target.
   * @param target The target from which to retrieve associated metric groups
   * @return A span<astl_metric_group_handle_t> containing all registered metric groups, or an error.
   */
  [[nodiscard]] virtual auto GetMetricGroups(const ITarget* target) const
      -> std::expected<std::span<const astl_metric_group_handle_t>, astl_status_code> = 0;

  /**
   * @brief Assign values such as name, description, etc to the given metric groups properties pointer.
   */
  auto virtual GetMetricGroupProperties(astl_metric_group_handle_t      group,
                                        astl_metric_group_properties_t* properties) const -> astl_status_code = 0;

  /**
   * @brief Retrieve the metric handles associated with a given metric group instance
   */
  auto virtual GetMetricsInGroup(astl_metric_group_handle_t group) const
      -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> = 0;

  /**
   * @brief Removes all previously registered metrics, due to a reconfiguration event.
   *        Mostly intended for test harnesses.
   */
  auto virtual RemoveAllMetrics() -> void = 0;

  /**
   * @brief Summarize the metrics messages.
   *
   * This method should be called to create a summary for all the metrics.
   */
  [[nodiscard]] virtual auto SummarizeMetrics() -> astl_status_code = 0;

  friend auto ProtobufSerDes::Serialize(const IMetricManager& metric_manager, std::ostream& output_stream)
      -> astl_status_code;
};

}  // namespace astl

#endif  // I_METRIC_MANAGER_HPP_
