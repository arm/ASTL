// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_METRIC_MANAGER_HPP_
#define I_METRIC_MANAGER_HPP_

#include <memory>
#include <span>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "collector/collection_operations.hpp"
#include "common/astl_defines.hpp"
#include "common/clock_correlation.hpp"
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
   * @param properties A non-null pointer to a struct containing properties to fill in, comes from
   * astlGetCountersOnTarget API
   *
   * @return An astl_status_code indicating success or ASTL_STATUS_BAD_PARAM
   */
  [[nodiscard]] virtual auto GetCounterProperties(astl_counter_handle_t counter, astl_counter_props_t* properties) const
      -> astl_status_code = 0;

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
   * @brief Return the lifecycle-event EventMetric instance for the given target, or nullptr if none is registered.
   *
   * MetricManager records this pointer when an ASTL_NATIVE + ASTL_METRIC_EVENT metric is registered
   * via RegisterMetric.  Callers (e.g. Orchestrator::GetPauseMarkersSnapshot) use it to locate the
   * lifecycle-event metric's ProcessedSampledData in the processed-samples store.
   */
  [[nodiscard]] virtual auto GetLifecycleEventMetricOnTarget(const ITarget* target) const -> const IMetric* = 0;

  /**
   * @brief Inject a lifecycle event into the ASTL_NATIVE lifecycle-event metric for the given target.
   *
   * Directly emits one processed sample with @p event_value into the synthetic
   * astl_lifecycle_events.<target> metric so that lifecycle transitions (pause, resume,
   * crop boundaries) appear in the timeline returned by astlGetMetricSamplesOnTarget().
   * This is a no-op (returns SUCCESS) if no lifecycle-event metric is registered for @p target
   *
   * @param target      Target whose lifecycle-event metric should receive the event.
   * @param event_value uint64_t encoding of the lifecycle event type (see astl_lifecycle_event_type_t).
   * @param timestamp   Timestamp of the event (CLOCK_MONOTONIC_RAW).
   * @return ASTL_STATUS_SUCCESS or an error status.
   */
  [[nodiscard]] virtual auto InjectLifecycleEvent(const ITarget* target, uint64_t event_value,
                                                  ProcessedSampleTimestamp timestamp) -> astl_status_code = 0;

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
  [[nodiscard]] virtual auto GetProperties(astl_metric_handle_t metric, astl_metric_props_t* properties) const
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
   * @brief Store per-operation clock correlation data used to normalize raw sample timestamps.
   *
   * Must be called once per collection start, after ConfigureCollection and before the first
   * ProcessRawSamples call.  Existing entries for the same OperationIds are replaced so that
   * re-starting collection always uses fresh anchors.
   *
   * @param correlations Map from OperationId to paired (CLOCK_MONOTONIC_RAW, native-clock) snapshots.
   */
  virtual auto SetClockCorrelations(const ClockCorrelationMap& correlations) -> void = 0;

  /**
   * @brief Return a snapshot of the current per-operation clock correlation map.
   *
   * Used by crop operations to convert raw hardware clock ticks to CLOCK_MONOTONIC_RAW
   * nanoseconds when filtering the on-disk raw sample cache file.
   *
   * @return A copy of the current ClockCorrelationMap.
   */
  [[nodiscard]] virtual auto GetClockCorrelations() const -> ClockCorrelationMap = 0;

  /**
   * @brief Clear stale operation routing, clock correlations, and metric state for one target.
   *
   * Reconfiguration assigns fresh OperationIds. After the collector accepts the new configuration,
   * the metric manager removes any previous target-local OperationIds that are not present in
   * active_operation_ids so stale mappings from previous configurations do not accumulate.
   *
   * Passing an empty active_operation_ids span clears all operation routing for this target only.
   * ClearCollectionOperationState() is broader: it clears operation state for every target.
   *
   * @param target Target whose stale operation state should be cleared.
   * @param active_operation_ids OperationIds present in the accepted replacement configuration.
   * @return ASTL_STATUS_SUCCESS on success, or ASTL_STATUS_BAD_ARGUMENT if target is null.
   */
  [[nodiscard]] virtual auto ClearStaleOperationStateForTarget(const ITarget*               target,
                                                               std::span<const OperationId> active_operation_ids)
      -> astl_status_code = 0;

  /**
   * @brief Drop collection-scoped operation routing, clock correlations, and metric previous-sample state.
   *
   * This does not unregister metric/counter definitions. It is intended for a global clean
   * collection boundary before OperationIds are reused.
   */
  virtual auto ClearCollectionOperationState() -> void = 0;

  /**
   * @brief Reset all metric and counter instances associated with a target.
   *
   * This is primarily used before replaying cached raw samples back through the
   * metric pipeline so stateful metrics (for example delta/rate metrics) do not
   * retain stale "previous sample" state from an earlier processing pass.
   *
   * @param target Target whose metric state should be reset.
   * @return ASTL_STATUS_SUCCESS on success, or ASTL_STATUS_BAD_ARGUMENT if target is null.
   */
  [[nodiscard]] virtual auto ResetMetricsOnTarget(const ITarget* target) -> astl_status_code = 0;

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
  auto virtual GetMetricGroupProperties(astl_metric_group_handle_t group, astl_metric_group_props_t* properties) const
      -> astl_status_code = 0;

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
