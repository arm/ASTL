// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_API_IMPL_HPP_
#define ASTL_API_IMPL_HPP_

#include <atomic>
#include <functional>  // for std::reference_wrapper in expected return types
#include <memory>
#include <mutex>
#include <vector>

#include "astl/astl.h"
#include "astl_file_interface.hpp"
#include "collector/i_collector_manager.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "metric/i_counter.hpp"
#include "metric/i_metric_manager.hpp"
#include "output/i_output_manager.hpp"
#include "target.hpp"
#include "topology/i_topology_manager.hpp"

static_assert(sizeof(astl_value_t) == sizeof(double),
              "astl_value_t union should not change size for ABI compatibility");

namespace astl {

namespace fs = std::filesystem;

class Orchestrator : public IRawSampleSink, public IProcessedSampleSink {
 public:
  /**
   * @brief Per-target metric collection lifecycle state.
   *
   * Progression: UNCONFIGURED -> CONFIGURED -> STARTING -> STARTED -> (PAUSED <-> STARTED)* -> STOPPED
   *
   * Semantics:
   *  - UNCONFIGURED: no collection configured.
   *  - CONFIGURED: Configure*Collection succeeded; ready to start.
   *  - STARTING: start operation is in flight; used to serialize concurrent start attempts.
   *  - STARTED: active sampling (entered via StartCollection or ResumeCollection).
   *  - PAUSED: sampling suspended (entered via PauseCollection; configuration retained).
   *  - STOPPED: terminal for current configuration (entered via StopCollection).
   *
   * Valid transitions:
   *  - CONFIGURED -> STARTING -> STARTED
   *  - STARTING -> CONFIGURED (on start failure)
   *  - STARTED -> PAUSED
   *  - PAUSED -> STARTED
   *  - STARTED|PAUSED -> STOPPED
   *
   * State is tracked per target in _target_collection_states (mutex: _collection_state_mutex).
   * Pause/resume timestamps recorded in _target_pause_timestamps / _target_resume_timestamps.
   */
  enum class TargetCollectionState { UNCONFIGURED, CONFIGURED, STARTING, STARTED, PAUSED, STOPPED };

  /**
   * @brief Convert a TargetCollectionState value to a readable string.
   * @param state lifecycle state enum.
   * @return std::string_view naming the state (points to static compiler-generated storage).
   */
  static auto TargetCollectionStateToString(TargetCollectionState state) -> std::string_view;

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
   *
   * @param cache_dir_path - Path to a directory where temporary files can be stored during ASTL file save/load
   */
  Orchestrator(std::unique_ptr<ITopologyManager> topology_manager, std::unique_ptr<ICollectorManager> collector_manager,
               std::unique_ptr<IMetricManager> metric_manager, std::unique_ptr<IOutputManager> output_manager,
               fs::path cache_dir_path);

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
                                 std::unique_ptr<IOutputManager> output_manager, fs::path cache_dir_path) -> void;

  /**
   * @brief Return a reference to the single Orchestrator instance
   *        If one hasn't been constructed yet, a default one with no collectors,
   *        metrics, or targets will be created in a thread-safe way.
   *
   * @return a reference to an owning pointer to Orchestrator. Will return nullptr before InitializeInstance is called
   */
  static auto GetInstance() noexcept
      -> std::expected<std::reference_wrapper<std::unique_ptr<Orchestrator>>, astl_status_code>;
  /**
   * @brief Returns true if the global Orchestrator instance has been explicitly initialized.
   *
   * Unlike GetInstance(), this function will NOT attempt lazy construction. It is used by the
   * C API wrapper layer to gate lifecycle operations that require prior
   * `Orchestrator::GetInstance()` invocation.
   */
  static auto IsInitialized() -> bool { return instance_ != nullptr; }

  /**
   * @brief Destroy the current singleton instance, if any.
   *
   * This is primarily intended for tests and explicit reload workflows.
   */
  static auto ResetInstance() -> void;

  /**
   * @brief Test-only helper to atomically swap the global singleton instance.
   *
   * This bypasses lazy construction and is intended for C++ test harnesses.
   *
   * @param new_instance Replacement singleton instance (may be null to clear).
   * @return The previous singleton instance.
   */
  static auto SwapInstanceForTest(std::unique_ptr<Orchestrator> new_instance) -> std::unique_ptr<Orchestrator>;

  /**
   * @brief Serialize the current orchestrator state into its cache directory.
   *
   * This writes the topology + metric manager protobuf files into the cache dir.
   * It does not create a final .astl archive.
   */
  static auto SaveStateToCacheDir() -> astl_status_code;

  /**
   * @brief Save the current orchestrator state to an ASTL file on disk.
   *
   * @param file_path The path to the output ASTL file.
   */
  static auto SaveToFile(fs::path file_path) -> astl_status_code;

  /**
   * @brief Load an orchestrator state from an ASTL file on disk.
   *
   * @param file_path The path to the input ASTL file.
   * @param cache_dir_path The path to a directory where temporary files can be stored during loading.
   */
  static auto LoadFromFile(fs::path file_path, fs::path cache_dir_path) -> astl_status_code;

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
  auto ConfigureCounterCollection(const ITarget *target, const astl_collection_params_t *collection_params,
                                  std::span<const astl_counter_handle_t> counters) -> astl_status_code;

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
  auto ConfigureMetricCollection(const ITarget *target, const astl_collection_params_t *collection_params,
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
   * @brief Apply the previously configured collection on the given target, then pause it before returning.
   *
   * This is equivalent to transitioning CONFIGURED -> STARTED -> PAUSED as one API operation.
   *
   * @param target The target with an active collection configuration
   * @note ConfigureCounterCollection or similar should be called first
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - ASTL_STATUS_PAUSE_UNSUPPORTED: underlying collector cannot pause after starting
   *   - others: according to individual Collector implementations
   */
  auto StartCollectionPaused(const ITarget *target) -> astl_status_code;

  /**
   * @brief Internal rollback helper used to restore a successfully started target back to CONFIGURED.
   *
   * This stops collection without emitting final outputs or transitioning the target to STOPPED.
   */
  auto RollbackStartedCollectionToConfigured(const ITarget *target) -> astl_status_code;

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
   * @brief Pause the collection of samples while retaining configuration and already gathered samples.
   *
   * Preconditions: target exists; collection is STARTED.
   * Postconditions: state transitions to PAUSED; collectors quiesced.
   *
   * @return status:
   *  - ASTL_STATUS_SUCCESS on success
   *  - ASTL_STATUS_INVALID_TARGET_HANDLE if target not managed
   *  - ASTL_STATUS_COLLECTION_NOT_RUNNING if collection not STARTED
   *  - ASTL_STATUS_COLLECTION_ALREADY_PAUSED if already PAUSED
   *  - ASTL_STATUS_PAUSE_UNSUPPORTED if underlying collector cannot pause
   *  - ASTL_STATUS_INTERNAL_ERROR if a manager dependency is missing
   *
   * Thread-safety: acquires internal lifecycle mutex for state check and transition.
   * Idempotence: repeated calls while PAUSED return ASTL_STATUS_COLLECTION_ALREADY_PAUSED.
   */
  auto PauseCollection(const ITarget *target) -> astl_status_code;

  /**
   * @brief Resume collection after a prior pause.
   *
   * Preconditions: target exists; state is PAUSED.
   * Postconditions: state transitions to STARTED; sampling restarts.
   *
   * @return status:
   *  - ASTL_STATUS_SUCCESS on success
   *  - ASTL_STATUS_INVALID_TARGET_HANDLE if target not managed
   *  - ASTL_STATUS_COLLECTION_NOT_PAUSED if not currently PAUSED
   *  - ASTL_STATUS_COLLECTION_ALREADY_RUNNING if already STARTED
   *  - ASTL_STATUS_RESUME_UNSUPPORTED if underlying collector cannot resume
   *  - ASTL_STATUS_INTERNAL_ERROR if a manager dependency is missing
   *
   * Thread-safety: acquires internal lifecycle mutex for state check and transition.
   * Idempotence: repeated calls while STARTED return ASTL_STATUS_COLLECTION_ALREADY_RUNNING.
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
   * @brief Get current lifecycle state for a managed target.
   * @param target Target pointer (must be non-null and owned by this orchestrator).
   * @return expected with TargetCollectionState or error:
   *   - ASTL_STATUS_BAD_ARGUMENT if target is null
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE if not found
   *
   * States:
   *   - UNCONFIGURED: target present but collection not yet configured
   *   - CONFIGURED: ConfigureMetricCollection succeeded for target
   *   - STARTED: StartCollection succeeded (or ResumeCollection from PAUSED)
   *   - PAUSED: PauseCollection succeeded
   *   - STOPPED: StopCollection succeeded
   *
   * Thread-safety: acquires lifecycle mutex; returns copy of enum value.
   */
  auto GetTargetCollectionState(const ITarget *target) const -> std::expected<TargetCollectionState, astl_status_code>;

  /**
   * @brief Snapshot all target lifecycle states.
   * Thread-safe: locks internal mutex and returns a copy of the mapping.
   *
   * @return unordered_map keyed by ITarget* with current TargetCollectionState (see GetTargetCollectionState).
   * Copy semantics: caller receives independent container safe against subsequent mutations.
   */
  auto GetAllTargetCollectionStates() const -> std::unordered_map<const ITarget *, TargetCollectionState>;

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

  // Thread-safe copy-out API used by C-wrapper reads.
  // Returning by value prevents callers from observing concurrent mutation of
  // the internal processed-sample map.
  auto GetProcessedSamplesSnapshot() const -> ProcessedSamplesMap {
    std::lock_guard lock{_processed_samples_mtx};
    return _processed_samples;
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

  /**
   * @brief Return a snapshot of pause-event timestamps per target.
   *
   */
  auto GetPauseMarkersSnapshot() const -> PauseMarkersMap;

 private:
  auto StartCollectionImpl(const ITarget *target, bool start_paused) -> astl_status_code;

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

  enum class FinalOutputEmissionState { NOT_EMITTED, EMITTING, EMITTED };

  /**
   * @brief Clear per-target cached collection artifacts before a fresh collection lifecycle.
   *
   * This drops any in-memory raw / processed samples and removes the target's on-disk raw
   * sample cache file so subsequent reads do not replay stale samples from an earlier
   * configuration.
   */
  auto        ResetTargetCollectionArtifacts(const ITarget *target) -> astl_status_code;
  auto        ResetFinalOutputEmissionState() -> void;
  static auto TryBeginFinalOutputEmission(std::atomic<FinalOutputEmissionState> &emission_state) -> bool;
  static auto FinishFinalOutputEmission(std::atomic<FinalOutputEmissionState> &emission_state, bool emission_succeeded)
      -> void;

  /**
   * @brief Trigger final-emission processed-sample rebuild once per lifecycle.
   *
   * Subsequent calls within the same final emission phase are no-ops.
   */
  auto EnsureFinalEmissionProcessedSamplesRebuilt() const -> void;

  /**
   * @brief Rebuild processed samples for all known (target, metric) pairs.
   *
   * Triggers lazy population via GetProcessedMetricSamples() so final outputs can
   * consume a fully populated _processed_samples map.
   */
  auto RebuildProcessedSamplesForAllTargets() const -> void;

  /**
   * @brief Register (once per target) a synthetic ASTL_METRIC_EVENT metric that records pause events.
   *
   */
  auto RegisterPauseResumeEventMetricForTarget(const ITarget *target) -> astl_status_code;

  /**
   * @brief Global singleton initialization mutex.
   *
   * Guards `instance_` construction/teardown paths used by InitializeInstance/GetInstance/ResetInstance.
   */
  static auto                          GetMutex() -> std::mutex &;
  static std::unique_ptr<Orchestrator> instance_;  // singleton instance pointer
  // Per-target collection lifecycle tracking (see public Doxygen block for semantics)
  std::unordered_map<const ITarget *, TargetCollectionState> _target_collection_states;  // lifecycle state per target
  std::unordered_map<const ITarget *, std::chrono::steady_clock::time_point>
      _target_pause_timestamps;  // last pause time
  std::unordered_map<const ITarget *, std::chrono::steady_clock::time_point>
                     _target_resume_timestamps;  // last resume time
  mutable std::mutex _collection_state_mutex;    // protects lifecycle state and pause/resume timestamp maps

  std::unique_ptr<ITopologyManager>     _topology_manager;   // manages the set of Targets
  std::unique_ptr<ICollectorManager>    _collector_manager;  // manages the collection of raw samples
  std::unique_ptr<IMetricManager>       _metric_manager;     // manages the processing of raw samples into metrics
  std::unique_ptr<IOutputManager>       _output_manager;     // manages the output of processed samples
  RawSamplesMap                         _raw_samples;        // collected raw samples, organized by target
  mutable std::mutex                    _raw_samples_mtx;    // protect the _raw_samples container
  mutable ProcessedSamplesMap           _processed_samples;  // processed metric samples, organized by target and metric
  mutable std::mutex                    _processed_samples_mtx;  // protect the _processed_samples container
  std::atomic<FinalOutputEmissionState> _perfetto_emission_state{FinalOutputEmissionState::NOT_EMITTED};
  std::atomic<FinalOutputEmissionState> _intervalcsv_emission_state{FinalOutputEmissionState::NOT_EMITTED};
  mutable std::atomic<bool>             _final_rebuild_attempted{false};
  std::filesystem::path                 _cache_dir;  // temporary directory to save and load from ASTL file
};

}  // namespace astl

#endif  // ASTL_API_IMPL_HPP_
