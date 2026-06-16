// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
/*******************************************************************************
 * @file metric_manager.hpp
 * @brief Defines MetricManager, which handles registration of metrics,
 *        exposes available metrics, and constructs SCMI operation sequences.
 ******************************************************************************/

#ifndef METRIC_MANAGER_HPP_
#define METRIC_MANAGER_HPP_

#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "collector/collection_operations.hpp"
#include "common/capabilities.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "i_metric_manager.hpp"
#include "metric/counter.hpp"
#include "metric/metric_group.hpp"

namespace astl {

class MetricManagerTestAccessor;

/**
 * @struct MetricHandle - internal representation of a astl_metric_handle_t
 * @brief Holds details of a single metric including its configuration and associated targets.
 */
struct MetricHandle {
  std::unique_ptr<MetricConfig>                                config;  //< Configuration that generated this metric
  std::unordered_map<const ITarget*, std::unique_ptr<IMetric>> target_to_metric_map;

  static constexpr bool kSerializable{true};
};

/**
 * @class MetricManager
 * @brief Implements IMetricManager to manage metrics and generate collection operations.
 */
class MetricManager : public IMetricManager, public IProcessedSampleSink {
  friend class MetricManagerTestAccessor;  // for unit test injection of mock metrics
 public:
  using MetricGroupDescriptionMap  = std::unordered_map<std::string, std::string>;
  using TargetToMetricsMap         = std::unordered_map<const ITarget*, std::vector<astl_metric_handle_t>>;
  using OperationToMetricMap       = std::unordered_map<uint32_t, IMetric*>;
  using TargetOperationToMetricMap = std::unordered_map<const ITarget*, OperationToMetricMap>;
  using LifecycleEventMetricMap    = std::unordered_map<const ITarget*, IMetric*>;

  ~MetricManager() override = default;

  // MetricManager cannot be copied
  MetricManager()                                = delete;
  MetricManager(const MetricManager&)            = delete;
  MetricManager& operator=(const MetricManager&) = delete;
  MetricManager(MetricManager&&)                 = delete;
  MetricManager& operator=(MetricManager&&)      = delete;

  explicit MetricManager(const Capabilities& capabilities, MetricGroupDescriptionMap metric_group_descriptions = {})
      : _capabilities(capabilities), _metric_group_descriptions(std::move(metric_group_descriptions)) {}

  /**
   * @brief Helper to look up a ICounter handle representing a counter for a specific target from a metric API handle
   */
  [[nodiscard]] auto GetCounterOnTarget(astl_counter_handle_t counter_handle, const ITarget* target) const
      -> std::expected<IMetric*, astl_status_code> override;

  /**
   * @brief Register the counter.
   *
   * This method is called by the orchestrator to register a new counter.
   */
  [[nodiscard]] auto RegisterCounter(std::unique_ptr<MetricConfig>      counter_config,
                                     std::vector<const ITarget*> const& targets) -> astl_status_code override;

  /**
   * @brief Get the number of available counters for the given target.
   * @param target The target from which to retrieve associated counters
   * @return The number of available counters for the given target, or an error.
   */
  auto GetNumAvailableCounters(const ITarget* target) const -> size_t override;

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
  [[nodiscard]] auto GetAvailableCounters(const ITarget* target) const
      -> std::expected<std::span<const astl_counter_handle_t>, astl_status_code> override;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   *
   * @param counter The counter API handle for potentially many identical counters that differ only in their target
   * @param properties A non-null pointer to a struct containing that GetProperties will fill in
   *
   * @return An astl_status_code indicating success or ASTL_STATUS_BAD_PARAM
   */
  [[nodiscard]] auto GetCounterProperties(astl_counter_handle_t counter, astl_counter_props_t* properties) const
      -> astl_status_code override;

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
  [[nodiscard]] auto GetCounterRequiredOperations(std::span<const astl_counter_handle_t> counters,
                                                  const ITarget*                         target)
      -> std::expected<CollectionOperations, astl_status_code> override;

  // IMetricManager implementation
  /**
   * @brief Helper to look up a IMetric handle for a specific target from a metric API handle
   */
  auto GetMetricOnTarget(astl_metric_handle_t metric_handle, const ITarget* target) const
      -> std::expected<IMetric*, astl_status_code> override;

  auto GetLifecycleEventMetricOnTarget(const ITarget* target) const -> const IMetric* override;

  auto InjectLifecycleEvent(const ITarget* target, uint64_t event_value, ProcessedSampleTimestamp timestamp)
      -> astl_status_code override;

  /**
   * @brief Register a sink to receive processed metric samples as they are produced.
   *
   * Multiple sinks may be registered (e.g. an output manager plus a test probe). Registration
   * is idempotent; attempting to register the same pointer twice returns success without changes.
   *
   * @param sink Non-null pointer to a sink implementation whose lifetime must outlive its
   *             unregistration or destruction of this manager.
   *             Must not be called from inside a SinkProcessedSamples callback.
   * @return ASTL_STATUS_SUCCESS on success, ASTL_STATUS_BAD_ARGUMENT if sink is null.
   */
  auto RegisterProcessedSampleSink(IProcessedSampleSink* sink) -> astl_status_code override;

  /**
   * @brief Unregister a previously registered processed sample sink.
   *
   * A no-op if the sink was not registered. After this call the sink will receive no further
   * callbacks.
   *
   * @param sink Pointer previously passed to RegisterProcessedSampleSink.
   *             Must not be called from inside a SinkProcessedSamples callback.
   * @return ASTL_STATUS_SUCCESS (even if sink not found), ASTL_STATUS_BAD_ARGUMENT if null.
   */
  auto UnregisterProcessedSampleSink(IProcessedSampleSink* sink) -> astl_status_code override;

  /**
   * @brief Register a new metric configuration.
   * @param metric_config Unique pointer to the MetricConfig to register.
   * @param targets       Collection of targets where this metric can be sampled
   *
   * @return ASTL_STATUS_SUCCESS on success, or an error code (e.g., ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE).
   */
  auto RegisterMetric(std::unique_ptr<MetricConfig> metric_config, std::vector<const ITarget*> const& targets)
      -> astl_status_code override;

  /**
   * @brief Get the number of available metrics for the given target.
   * @param target The target from which to retrieve associated metrics
   * @return The number of available metrics for the given target, or an error.
   */
  auto GetNumAvailableMetrics(const ITarget* target) const -> size_t override;

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
  auto GetProperties(astl_metric_handle_t metric, astl_metric_props_t* properties) const -> astl_status_code override;

  /**
   * @brief Build the sequence of operations required to collect the given metrics.
   * @param metrics Span of metric api handles for which to generate operations.
   * @param target The target for which to generate operations.
   * @return expected containing an CollectionOperations group of SCMI read operations on success,
   *         or an error code (e.g., BAD_ARGUMENT, UNSUPPORTED_COLLECTOR_TYPE).
   */
  auto GetRequiredOperations(std::span<const astl_metric_handle_t> metrics, const ITarget* target)
      -> std::expected<CollectionOperations, astl_status_code> override;

  /**
   * @brief Distribute collected sample data to registered metrics.
   * @param data Span of RawSampledData to process.
   * @return ASTL_STATUS_SUCCESS or an appropriate error code.
   */
  auto ProcessRawSamples(RawSamplesMap& raw_samples) -> astl_status_code override;

  /**
   * @brief Store per-operation clock correlation data used to normalize raw timestamps.
   *
   * Thread-safe; protected by _mutex.  Replaces any existing entry for the same OperationId.
   */
  auto SetClockCorrelations(const ClockCorrelationMap& correlations) -> void override;

  /**
   * @brief Return a snapshot of the current per-operation clock correlation map.
   */
  [[nodiscard]] auto GetClockCorrelations() const -> ClockCorrelationMap override;

  /**
   * @brief Reset all metric/counter instances associated with a target.
   *
   * Stateful metrics such as delta/rate metrics retain previous-sample state.
   * This helper clears that state before cached raw samples are replayed.
   */
  auto ResetMetricsOnTarget(const ITarget* target) -> astl_status_code override;

  /**
   * @brief Return the metric groups registered in the manager.
   *
   * @return A span<astl_metric_group_handle_t> containing all registered metric groups, or an error.
   */
  [[nodiscard]] auto GetMetricGroups() const -> std::span<const astl_metric_group_handle_t> override;

  /**
   * @brief Return the metric groups registered in the manager for a specific target.
   * @param target The target from which to retrieve associated metric groups
   * @return A span<astl_metric_group_handle_t> containing all registered metric groups, or an error.
   */
  [[nodiscard]] auto GetMetricGroups(const ITarget* target) const
      -> std::expected<std::span<const astl_metric_group_handle_t>, astl_status_code> override;

  /**
   * @brief Assign values such as name, description, etc to the given metric groups properties pointer.
   * @param group The metric group API handle
   * @param properties A non-null pointer to a struct that to be populated with info about the given group
   */
  auto GetMetricGroupProperties(astl_metric_group_handle_t group, astl_metric_group_props_t* properties) const
      -> astl_status_code override;

  /**
   * @brief Retrieve the metric handles associated with a given metric group instance
   */
  auto GetMetricsInGroup(astl_metric_group_handle_t group) const
      -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> override;

  /**
   * @brief Removes all previously registered metrics, due to a reconfiguration event.
   *        Mostly intended for test harnesses.
   */
  auto RemoveAllMetrics() -> void override;

  /**
   * @brief Finalize and summarize metrics after data processing.
   * @return ASTL_STATUS_SUCCESS or an appropriate error code.
   */
  auto SummarizeMetrics() -> astl_status_code override;

  /**
   * @brief Look up the target associated with a specific metric instance.
   *
   * Some metrics may exist on multiple targets; this helper returns the target that owns the
   * provided metric pointer.
   *
   * @param metric Metric instance pointer.
   * @return expected containing target on success or ASTL_STATUS_BAD_ARGUMENT / NOT_FOUND.
   */
  auto GetTargetForMetric(const IMetric* metric) const -> std::expected<const ITarget*, astl_status_code>;

  // IProcessSampleSink implementation
  /**
   * @brief Receive processed samples from a metric and forward to registered sinks.
   *
   * This implements the `IProcessedSampleSink` interface allowing metrics to push their completed
   * samples back into the manager which then fan-outs to external sinks (e.g. outputs).
   *
   * @param metric Metric instance producing the samples.
   * @param processed_samples Span of processed samples valid for the duration of the call.
   * @return ASTL_STATUS_SUCCESS or a propagated error code if dispatch fails.
   */
  // Original interface override (without explicit target) required by IMetricManager
  auto SinkProcessedSamples(const IMetric* metric, std::span<const ProcessedSampledData> processed_samples)
      -> astl_status_code override;

  // Extended helper allowing direct target specification (used by metrics via RawMetric -> manager fan-out)
  auto SinkProcessedSamples(const ITarget* target, const IMetric* metric,
                            std::span<const ProcessedSampledData> processed_samples) -> astl_status_code override;

  /**
   * TODO (https://jira.arm.com/browse/ASTL-112) : remove this friend declaration
   *  once a way to inject Mock Metric to Metric Manager for testing is introduced..
   */
  friend class MetricManagerTestAccessor;

  friend auto ProtobufSerDes::Serialize(const MetricManager& metric_manager, std::ostream& output_stream)
      -> astl_status_code;

  friend auto ProtobufSerDes::Deserialize<std::unique_ptr<MetricManager>>(std::istream&,
                                                                          const std::vector<std::unique_ptr<ITarget>>&)
      -> std::expected<std::unique_ptr<MetricManager>, astl_status_code>;

 private:
  /**
   * @brief Determine whether a collector type required by a metric configuration is supported.
   *
   * This inspects the capabilities provided at construction and verifies the caller requested
   * collector type is available before registering metrics or generating operations that depend on it.
   *
   * @param required_collector_type Collector type required by a metric.
   * @return true if the capability is present, false otherwise.
   */
  auto IsCollectorTypeSupported(CollectorType required_collector_type) const -> bool;

  /**
   * @brief Check whether a metric with the given stable ID has already been registered.
   * @param metric_id Candidate metric identifier.
   * @return true if another registered metric already uses this ID.
   */
  auto IsMetricIdRegistered(const std::string& metric_id) const -> bool;

  /**
   * @brief Helper for RegisterMetric to add metric to groups based on its config
   * @param metric_handle The metric handle to add to groups
   * @param metric_config The metric config used to determine group membership
   * @param targets       The targets associated with this metric
   */
  auto AddMetricToGroups(astl_metric_handle_t metric_handle, const MetricConfig* metric_config,
                         const std::vector<const ITarget*>& targets) -> astl_status_code;

  /**
   * @brief Add the given (potentially newly discovered) group to all targets
   * @param group_handle The metric group handle to add to targets
   * @param targets      The targets to which the group should be added
   */
  auto AddMetricGroupToTargets(astl_metric_group_handle_t group_handle, const std::vector<const ITarget*>& targets)
      -> astl_status_code;

  Capabilities _capabilities;

  std::unordered_set<IProcessedSampleSink*> _registered_processed_sample_sinks;

  // Collection of all registered Counters
  // stored as unique_ptrs so we can safely store pointers to them in _target_to_counters_map
  // might be worth reworking that: store std::vector<CounterHandle> (same with metric handles) and change how
  // _target_to*_maps work, how GetAvailableMetrics returns values, etc.
  std::vector<std::unique_ptr<CounterHandle>> _counter_handles;
  // each Target supports multiple different counters
  std::unordered_map<const ITarget*, std::vector<astl_counter_handle_t>> _target_to_counters_map;
  // note that since counters implement IMetric, their operations are routed through
  // _target_to_operation_to_metric_map just like regular metrics.

  // an API astl_metric_handle_t can represent one metric runnable on multiple targets
  // our internal representation of astl_metric_handle is `MetricHandle*`
  // note, we store pointers to MetricHandle (not just MetricHandle) to avoid invalidation of handles
  // when _metric_handles resizes, since these handles are used in _target_to_metrics_map
  std::vector<std::unique_ptr<MetricHandle>> _metric_handles;

  // each Target supports multiple different metrics
  TargetToMetricsMap _target_to_metrics_map;

  // each Target supports multiple different metric groups
  std::unordered_map<const ITarget*, std::vector<astl_metric_group_handle_t>> _target_to_metric_groups_map;

  // Maps operation IDs to their corresponding metrics for each target.
  TargetOperationToMetricMap _target_to_operation_to_metric_map;

  // Tracks the single lifecycle-event EventMetric per target (registered by Orchestrator via
  // RegisterMetric with ASTL_NATIVE + ASTL_METRIC_EVENT).
  LifecycleEventMetricMap _target_to_lifecycle_event_metric;

  // Optional metadata loaded from config/groups/metric_groups.json.
  MetricGroupDescriptionMap _metric_group_descriptions;

  // The internal representation of metric groups
  // stored as unique_ptrs so that they're not invalidated when adding new entries,
  // as the raw pointers are exposed via astl_metric_group_handle_t through _metric_group_api_handles.
  std::vector<std::unique_ptr<MetricGroup>> _metric_groups;

  // Same data as in `_metric_groups`, but using raw pointers to MetricGroups for exposure.
  std::vector<astl_metric_group_handle_t> _metric_group_api_handles;

  // Coarse-grained mutex for all shared registries/maps used by API and ingestion paths.
  mutable std::mutex _mutex;

  // Per-operation clock correlations set at collection-start time.
  // Used by ProcessRawSamples to translate native-clock timestamps to CLOCK_MONOTONIC_RAW.
  ClockCorrelationMap _clock_correlations;
};

}  // namespace astl

#endif  // METRIC_MANAGER_HPP_
