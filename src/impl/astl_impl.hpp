#ifndef ASTL_API_IMPL_HPP_
#define ASTL_API_IMPL_HPP_

#include <functional>  // for std::reference_wrapper in expected return types
#include <memory>

#include "astl/astl.h"
#include "collector/i_collector_manager.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "metric/i_metric_manager.hpp"
#include "output/i_output_manager.hpp"
#include "target.hpp"
#include "topology/i_topology_manager.hpp"

static_assert(sizeof(astl_value_t) == sizeof(double),
              "astl_value_t union should not change size for ABI compatibility");

namespace astl {

class Orchestrator : public IRawSampleSink, public IProcessedSampleSink {
 public:
  /**
   * @brief Create a fully armed and operational Orchestrator from the necessary parts.
   *        One of Orchestrator's class invariants is that it has non-null topology, collector, and metric managers.
   *
   * @param topology_manager - Used to discover the hardware components (targets) on the current platform.
   *
   * @param collector_manager - Can be given a set of operations and hints on how to run them,
   *                            and then sample the data on an appropriate data source
   *
   * @param metric_manager - Can turn a set of desired metrics into a set of operations to collect,
   *                         then post-process the sampled data
   *
   * @param output_manager - Can turn a set of processed metric samples into desired output formats
   */
  Orchestrator(std::unique_ptr<ITopologyManager> topology_manager, std::unique_ptr<ICollectorManager> collector_manager,
               std::unique_ptr<IMetricManager> metric_manager, std::unique_ptr<IOutputManager> output_manager);

  ~Orchestrator() override;

  // forbid copy
  Orchestrator(Orchestrator const &)            = delete;
  Orchestrator &operator=(Orchestrator const &) = delete;
  // forbid move construction for now
  // (if you add them later, be sure to move handle the _collector_manager's sample-sink registration)
  Orchestrator(Orchestrator &&other)            = delete;
  Orchestrator &operator=(Orchestrator &&other) = delete;

  /**
   * @brief Initialize the static singleton instance of Orchestrator, to be retrieved later through GetInstance
   *
   * @param topology_manager - Used to discover the hardware components (targets) on the current platform.
   *
   * @param collector_manager - Can be given a set of operations and hints on how to run them,
   *                            and then sample the data on an appropriate data source
   *
   * @param metric_manager - Can turn a set of desired metrics into a set of operations to collect,
   *                         then post-process the sampled data
   * @param output_manager - Can turn a set of processed metric samples into desired output formats
   */
  static auto InitializeInstance(std::unique_ptr<ITopologyManager>  topology_manager,
                                 std::unique_ptr<ICollectorManager> collector_manager,
                                 std::unique_ptr<IMetricManager>    metric_manager,
                                 std::unique_ptr<IOutputManager>    output_manager) -> void;

  /**
   * @brief Return a reference to the single Orchestrator instance
   *        If one hasn't been constructed yet, a default one with no collectors,
   *        metrics, or targets will be created in a thread-safe way.
   *        astlInitialize will use this returned reference to assign a new Orchestrator that may
   *        have more complex internals
   *
   * @return a reference to an owning pointer to Orchestrator. Will return nullptr before InitializeInstance is called
   */
  static auto GetInstance() -> std::unique_ptr<Orchestrator> &;

  /**
   * @brief Returns a const reference to the set of Targets managed by this orchestrator.
   */
  auto GetTargets() const -> std::vector<std::unique_ptr<ITarget>> const &;

  /**
   * @brief Reassign the set of Targets managed by this orchestrator.
   *
   * Refactor - We probably want to provide a more controlled interface for modifying the target list
   *  For example, we could add member functions to enable/disable specific targets or
   *  modify the list internally when we read the configuration.
   */
  auto SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code;

  /**
   * @brief For a given target, enable collection on a set of measurable Counters.
   *
   * @param target The target from which the collection will be sampled
   * @param collection_params Specifies how the collection should be gathered
   * @param counters The set of data points to collect
   *
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET: one of the given counters is not associated with the target
   */
  auto ConfigureCounterCollection(const ITarget *target, const astl_collection_parameters_t *collection_params,
                                  std::span<const ICounter *> counters) -> astl_status_code;

  /**
   * @brief For a given target, enable collection on a set of measurable Metrics.
   *
   * @param target The target from which the collection will be sampled
   * @param collection_params Specifies how the collection should be gathered
   * @param metrics The set of data points to collect
   *
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET: one of the given metrics is not associated with the target
   */
  auto ConfigureMetricCollection(const ITarget *target, const astl_collection_parameters_t *collection_params,
                                 std::span<const astl_metric_handle_t> metrics) -> astl_status_code;

  /**
   * @brief Apply the previously configured collection on the given target
   *
   * Attempts to enable any data sources set up by ConfigureCounterCollection or similar, and may take initial sample
   * @param target The target with an active collection configuration
   * @note ConfigureCounterCollection or similar should be called first
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  auto StartCollection(const ITarget *target) -> astl_status_code;

  /**
   * @brief Collect one sample of data on a target with an active configured collection
   *
   * @param target The target with an active collection configuration
   * @note ConfigureCounterCollection or similar should be called before ReadImmediate
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  auto ReadImmediate(const ITarget *target) -> astl_status_code;

  /**
   * @brief Stop the collection of samples, but leave configuration in place
   *
   * @param target The target with an active collection configuration
   * @note StartCollection should be called before this
   * @note Re-enable collection with ResumeCollection
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  auto PauseCollection(const ITarget *target) -> astl_status_code;

  /**
   * @brief Re-enable the collection of samples, based on previous configuration
   *
   * @param target The target with an active collection configuration
   * @note PauseCollection should be called before this
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  auto ResumeCollection(const ITarget *target) -> astl_status_code;

  /**
   * @brief Stop the collection of samples
   *
   * This stops collecting samples, restores original system configuration (disabling data sources),
   * and captures any final samples necessary.
   *
   * @param target The target with an active collection configuration
   * @note StartCollection should be called before this
   * @note To re-enable collection, StartCollection should be sufficient.
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  auto StopCollection(const ITarget *target) -> astl_status_code;

  /**
   * @brief Return the number of collected samples for a given counter on the given target
   * @param target The target on which collection was configured and performed
   * @param counter The specific data source that was sampled
   *
   * @return a std::expected pair with either:
   *   - a value: the count of samples taken for the given ICounter on the target
   *   - OR an error status code such as an invalid handle or bad argument
   */
  auto GetCounterSampleCount(const ITarget *target, const ICounter *counter) const
      -> std::expected<uint32_t, astl_status_code>;

  // TODO(ASTL-58): when OutputManager is implemented, revisit to see if GetMetricManager is even needed
  /**
   * @brief Return a reference to a pointer to the MetricManager, used to enumerate metrics
   */
  auto GetMetricManager() const -> const std::unique_ptr<IMetricManager> & { return _metric_manager; }

  /**
   * @brief Return a reference to a pointer to the OutputManager, used to enumerate outputs
   */
  auto GetOutputManager() const -> const std::unique_ptr<IOutputManager> & { return _output_manager; }

  /**
   * @brief Implementation of the IRawSampleSink interface - Receives raw samples from CollectorManager
   */
  auto SinkRawSamples(const ITarget *target, std::span<RawSampledData> raw_samples) -> astl_status_code override;

  // std::expected cannot hold reference types directly (C++23 lib enforces this),
  // so expose a reference via std::reference_wrapper
  auto GetProcessedSamples() -> std::expected<std::reference_wrapper<ProcessedSamplesMap>, astl_status_code> {
    return std::reference_wrapper<ProcessedSamplesMap>(_processed_samples);
  }

  /**
   * @brief Retrieve the collected samples for the given target and metric,
   *        or an error if the target+metric combination isn't valid
   */
  auto GetProcessedMetricSamples(const IMetric *metric, const ITarget *target) const
      -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code>;

  /**
   * @brief Implementation of the IProcessedSampleSink interface - Receives processed samples from MetricManager
   */
  auto SinkProcessedSamples(const ITarget *target, const IMetric *metric,
                            std::span<const ProcessedSampledData> processed_samples) -> astl_status_code override;

 private:
  /**
   * @brief Emit a summary CSV file of all processed samples if requested via environment variable.
   *
   * Logic:
   *  - Checks ASTL_OUTPUT_SUMMARY_CSV (empty -> no-op).
   *  - Uses OutputManager to dispatch with OutputType::SUMMARY_CSV (writer instantiated on-demand).
   *  - Non-blocking: any failure logged and ignored (overall StopCollection still returns success unless
   *    earlier steps failed).
   */
  auto EmitSummaryCsvIfRequested() -> void;

  /**
   * @brief Emit a Perfetto trace of all processed samples if requested via environment variable.
   *
   * Logic:
   *  - Checks ASTL_OUTPUT_PERFETTO (empty -> no-op).
   *  - Ensures one-time emission (subsequent StopCollection calls won't rewrite).
   *  - Uses OutputManager to dispatch with OutputType::PERFETTO (writer instantiated lazily there).
   *  - Non-blocking: any failure logged and ignored (overall StopCollection still returns success unless
   *    earlier steps failed).
   */
  auto EmitPerfettoTraceIfRequested() -> void;

  /**
   * @brief Emit an Interval CSV of all processed samples if requested via environment variable.
   *
   * Logic:
   *  - Checks ASTL_OUTPUT_INTERVAL_CSV (empty -> no-op).
   *  - Ensures one-time emission (subsequent StopCollection calls won't rewrite).
   *  - Uses OutputManager to dispatch with OutputType::INTERVAL_CSV (writer instantiated lazily there).
   *  - Non-blocking: any failure logged and ignored (overall StopCollection still returns success unless
   *    earlier steps failed).
   */
  auto EmitIntervalCsvIfRequested() -> void;

  static auto                        GetMutex() -> std::mutex &;  // manage thread-safe access   to singleton instance
  std::unique_ptr<ITopologyManager>  _topology_manager;           // manages the set of Targets
  std::unique_ptr<ICollectorManager> _collector_manager;          // manages the collection of raw samples
  std::unique_ptr<IMetricManager>    _metric_manager;             // manages the processing of raw samples into metrics
  std::unique_ptr<IOutputManager>    _output_manager;             // manages the output of processed samples
  RawSamplesMap                      _raw_samples;                // collected raw samples, organized by target
  mutable std::mutex                 _raw_samples_mtx;            // protect the _raw_samples container
  ProcessedSamplesMap                _processed_samples;  // processed metric samples, organized by target and metric
  mutable std::mutex                 _processed_samples_mtx;       // protect the _processed_samples container
  bool                               _perfetto_emitted{false};     // ensure single emission per collection lifecycle
  bool                               _intervalcsv_emitted{false};  // ensure single emission per collection lifecycle
};

}  // namespace astl

#endif  // ASTL_API_IMPL_HPP_
