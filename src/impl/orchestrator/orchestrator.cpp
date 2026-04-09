// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "orchestrator/orchestrator.hpp"

#include <algorithm>  // for std::max used in bulk reserve heuristic
#include <filesystem>
#include <fstream>
#include <functional>  // for std::reference_wrapper in expected return types
#include <magic_enum/magic_enum.hpp>

#include "astl/astl_errors.h"
#include "astl_defines.hpp"
#include "astl_logger.hpp"
#include "common/string_pool.hpp"
#include "common/system_info.hpp"
#include "config/configuration_manager.hpp"  // for ConfigurationManager::GetConfiguration
#include "orchestrator/orchestrator_builder.hpp"
#include "serdes/archive_utils.hpp"
#include "serdes/protobuf_serdes.hpp"

namespace astl {

// Definition of singleton instance pointer declared in orchestrator.hpp
std::unique_ptr<Orchestrator> Orchestrator::instance_{};

// Internal growth heuristic constants for sample vector reservations.
// Rationale: We previously used the expression size()*3/2 + 8 inline. Exposing
// the components as named constants improves readability, facilitates tuning,
// and ensures consistency across raw and processed sample sinks.
namespace {
// Growth heuristic: new_cap = current * kRawSampleGrowthNumerator / kRawSampleGrowthDenominator + kRawSampleGrowthBias
constexpr std::size_t kRawSampleGrowthNumerator   = 3;
constexpr std::size_t kRawSampleGrowthDenominator = 2;
constexpr std::size_t kRawSampleGrowthBias        = 8;  // small constant slack to reduce near-threshold reallocs

// Processed samples use the same heuristic currently; kept distinct so future tuning can diverge independently.
// Growth heuristic for processed samples (kept distinct for future independent tuning)
constexpr std::size_t kProcSampleGrowthNumerator   = 3;
constexpr std::size_t kProcSampleGrowthDenominator = 2;
constexpr std::size_t kProcSampleGrowthBias        = 8;

// TODO(ASTL-242): Investigate optimal max batch size
constexpr std::size_t kMaxSamplesPerBatch = 1024;
}  // namespace

namespace fs = std::filesystem;

Orchestrator::Orchestrator(std::unique_ptr<ITopologyManager>  topology_manager,
                           std::unique_ptr<ICollectorManager> collector_manager,
                           std::unique_ptr<IMetricManager>    metric_manager,
                           std::unique_ptr<IOutputManager> output_manager, fs::path cache_dir_path)
    : _topology_manager{std::move(topology_manager)},
      _collector_manager{std::move(collector_manager)},
      _metric_manager{std::move(metric_manager)},
      _output_manager{std::move(output_manager)},
      _cache_dir{cache_dir_path} {
  if (!_topology_manager || !_collector_manager || !_metric_manager || !_output_manager) {
    throw std::invalid_argument(
        "Orchestrator requires non-null inputs for topology, collector, metric, and output managers.");
  }
  // Registration return codes intentionally ignored: constructor cannot recover.
  // If registration fails, later operations using sinks will surface errors.
  (void)_collector_manager->RegisterRawSampleSink(this);
  (void)_metric_manager->RegisterProcessedSampleSink(this);
  // Initialize state for known targets
  for (auto const &target_ptr : _topology_manager->GetTargets()) {
    _target_collection_states[target_ptr.get()] = TargetCollectionState::UNCONFIGURED;
  }
}

Orchestrator::~Orchestrator() {
  // Best-effort cleanup; ignore status in destructor.
  std::error_code err_code;
  std::filesystem::remove_all(_cache_dir, err_code);
  if (err_code) {
    ASTL_LOG_WARNING("Orchestrator destructor failed to remove cache dir '{}': {}", _cache_dir.string(),
                     err_code.message());
  }
  if (_collector_manager) {
    (void)_collector_manager->UnregisterRawSampleSink(this);
  }
  if (_metric_manager) {
    (void)_metric_manager->UnregisterProcessedSampleSink(this);
  }
}

auto Orchestrator::InitializeInstance(std::unique_ptr<ITopologyManager>  topology_manager,
                                      std::unique_ptr<ICollectorManager> collector_manager,
                                      std::unique_ptr<IMetricManager>    metric_manager,
                                      std::unique_ptr<IOutputManager> output_manager, fs::path cache_dir_path) -> void {
  // Serialize singleton construction to prevent racing double-initialization.
  std::scoped_lock lock(GetMutex());
  if (!Orchestrator::instance_) {
    Orchestrator::instance_ =
        std::make_unique<Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                       std::move(metric_manager), std::move(output_manager), cache_dir_path);
  }
}

auto Orchestrator::GetInstance() noexcept
    -> std::expected<std::reference_wrapper<std::unique_ptr<Orchestrator>>, astl_status_code> {
  // Fast path under lock: instance already initialized.
  {
    std::scoped_lock lock(GetMutex());
    if (instance_) {
      return std::reference_wrapper<std::unique_ptr<Orchestrator>>(instance_);
    }
  }

  auto configuration = astl::ConfigurationManager::GetConfiguration();
  if (!configuration) {
    return std::unexpected(configuration.error());
  }

  // One-shot in-process override used by explicit load/import APIs.
  auto load_override = astl::ConfigurationManager::GetLoadFilePathOverride();
  if (load_override) {
    configuration->load_file_path = *load_override;
    astl::ConfigurationManager::SetLoadFilePathOverride(std::nullopt);
  }
  try {
    astl_status_code status = BuildOrchestrator(configuration.value());
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
  } catch (const std::bad_expected_access<astl_status_code> &ex) {
    ASTL_LOG_ERROR("Exception during Orchestrator initialization: {}", ex.what());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  // Re-check under lock after construction attempt.
  {
    std::scoped_lock lock(GetMutex());
    if (!instance_) {
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    return std::reference_wrapper<std::unique_ptr<Orchestrator>>(instance_);
  }
}

auto Orchestrator::ResetInstance() -> void {
  // Ensure teardown cannot race with concurrent GetInstance/InitializeInstance.
  std::scoped_lock lock(GetMutex());
  instance_.reset();
}

auto Orchestrator::SwapInstanceForTest(std::unique_ptr<Orchestrator> new_instance) -> std::unique_ptr<Orchestrator> {
  std::scoped_lock lock(GetMutex());
  auto             original_instance = std::move(instance_);
  instance_                          = std::move(new_instance);
  return original_instance;
}

auto Orchestrator::GetMutex() -> std::mutex & {
  static std::mutex initialization_mutex;
  return initialization_mutex;
}

auto Orchestrator::GetTargets() const -> std::vector<std::unique_ptr<ITarget>> const & {
  return _topology_manager->GetTargets();
}

auto Orchestrator::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code {
  // note, this function is really only of use in test harnesses right now.
  // there are strong dependencies from the metric manager's metric handles to targets managed by the topology manager.
  // so changing targets at runtime in a production scenario is not currently supported
  // - must delete all metrics to avoid use-after-free, and assume test code will add necessary new metrics
  _topology_manager->SetTargets(std::move(new_targets));
  // Synchronize internal collection state machine maps with updated targets list.
  // We cannot assume SetTargets is only called at initialization time; tests (and potential
  // dynamic topology discovery) may replace the target vector post-construction. Ensure each
  // new target receives an Unconfigured state entry and prune removed targets.
  {
    std::lock_guard                                            state_lock(_collection_state_mutex);
    std::unordered_map<const ITarget *, TargetCollectionState> updated_states;
    for (auto const &target_ptr : _topology_manager->GetTargets()) {
      const ITarget *raw            = target_ptr.get();
      auto           state_iterator = _target_collection_states.find(raw);
      if (state_iterator != _target_collection_states.end()) {
        // Preserve existing state if target persists.
        updated_states[raw] = state_iterator->second;
      } else {
        // Initialize brand new target entry.
        updated_states[raw] = TargetCollectionState::UNCONFIGURED;
      }
    }
    _target_collection_states.swap(updated_states);
  }
  _metric_manager->RemoveAllMetrics();
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ConfigureCounterCollection(const ITarget *target, const astl_collection_params_t *collection_params,
                                              std::span<const astl_counter_handle_t> counters) -> astl_status_code {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  if (!_metric_manager) {
    ASTL_LOG_ERROR("Orchestrator::ConfigureCounterCollection called with null MetricManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::ConfigureCounterCollection called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  // check for supported metrics
  auto available_counters_or_error = _metric_manager->GetAvailableCounters(target);
  if (!available_counters_or_error.has_value()) {
    return available_counters_or_error.error();
  }
  const auto available_counters = available_counters_or_error.value();
  for (const auto &counter : counters) {
    auto counter_index = std::ranges::find_if(
        available_counters, [counter](auto const available_counter) { return available_counter == counter; });
    if (counter_index == std::end(available_counters)) {
      ASTL_LOG_ERROR("Counter is not supported on target");
      return ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET;
    }
  }
  auto operations = _metric_manager->GetCounterRequiredOperations(counters, target);
  if (!operations) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  astl_status_code status =
      _collector_manager->ConfigureCollectionOnTarget(target, *collection_params, std::move(operations.value()));
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to configure collection on target: {}", astlStatusString(status));
    return status;
  }
  status = ResetTargetCollectionArtifacts(target);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to reset collection artifacts on target {}: {}", target->Name(), astlStatusString(status));
    return status;
  }
  ResetFinalOutputEmissionState();
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::CONFIGURED;
  }
  return status;
}

auto Orchestrator::ConfigureMetricCollection(const ITarget *target, const astl_collection_params_t *collection_params,
                                             std::span<const astl_metric_handle_t> metrics) -> astl_status_code {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  if (!_metric_manager) {
    ASTL_LOG_ERROR("Orchestrator::ConfigureMetricCollection called with null MetricManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::ConfigureMetricCollection called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  const auto expected_available_metrics = _metric_manager->GetAvailableMetrics(target);
  if (!expected_available_metrics.has_value()) {
    return expected_available_metrics.error();
  }
  const auto available_metrics = expected_available_metrics.value();

  // check for supported metrics
  for (const auto &metric : metrics) {
    auto metric_index = std::find_if(std::begin(available_metrics), std::end(available_metrics),
                                     [metric](auto const &available_metric) { return available_metric == metric; });
    if (metric_index == std::end(available_metrics)) {
      ASTL_LOG_ERROR("Metric is not supported on target");
      return ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET;
    }
  }
  auto operations = _metric_manager->GetRequiredOperations(metrics, target);
  if (!operations) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  astl_status_code status =
      _collector_manager->ConfigureCollectionOnTarget(target, *collection_params, std::move(operations.value()));
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to configure collection on target: {}", astlStatusString(status));
    return status;
  }
  status = ResetTargetCollectionArtifacts(target);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to reset collection artifacts on target {}: {}", target->Name(), astlStatusString(status));
    return status;
  }
  ResetFinalOutputEmissionState();
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::CONFIGURED;
  }

  // Register the synthetic pause-event metric for this target (no-op if already registered).
  const auto pause_metric_status = RegisterPauseResumeEventMetricForTarget(target);
  if (pause_metric_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_WARNING(
        "Orchestrator: pause-event metric registration failed for '{}' ({}); samples during this pause/resume window "
        "will not be ignored as expected.",
        target->Name(), astlStatusString(pause_metric_status));
  }

  return status;
}

auto Orchestrator::ResetTargetCollectionArtifacts(const ITarget *target) -> astl_status_code {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto stable_target_key = GetStableTargetKey(*target);

  {
    std::lock_guard raw_lock(_raw_samples_mtx);
    _raw_samples.erase(target);
  }
  {
    std::lock_guard processed_lock(_processed_samples_mtx);
    _processed_samples.erase(target);
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_pause_timestamps.erase(target);
    _target_resume_timestamps.erase(target);
  }

  std::error_code remove_error;
  fs::remove(_cache_dir / (stable_target_key + kAstlFileExtension), remove_error);
  if (remove_error && remove_error != std::errc::no_such_file_or_directory) {
    ASTL_LOG_ERROR("Failed to remove cached samples for target {}: {}", target->Name(), remove_error.message());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ResetFinalOutputEmissionState() -> void {
  _perfetto_emission_state.store(FinalOutputEmissionState::NOT_EMITTED, std::memory_order_release);
  _intervalcsv_emission_state.store(FinalOutputEmissionState::NOT_EMITTED, std::memory_order_release);
  _final_rebuild_attempted.store(false, std::memory_order_release);
}

auto Orchestrator::TryBeginFinalOutputEmission(std::atomic<FinalOutputEmissionState> &emission_state) -> bool {
  auto expected = FinalOutputEmissionState::NOT_EMITTED;
  return emission_state.compare_exchange_strong(expected, FinalOutputEmissionState::EMITTING, std::memory_order_acq_rel,
                                                std::memory_order_acquire);
}

auto Orchestrator::FinishFinalOutputEmission(std::atomic<FinalOutputEmissionState> &emission_state,
                                             bool                                   emission_succeeded) -> void {
  emission_state.store(emission_succeeded ? FinalOutputEmissionState::EMITTED : FinalOutputEmissionState::NOT_EMITTED,
                       std::memory_order_release);
}
auto Orchestrator::StartCollectionImpl(const ITarget *target, bool start_paused) -> astl_status_code {
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::StartCollection called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            state_iterator = _target_collection_states.find(target);
    if (state_iterator == _target_collection_states.end()) {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
    switch (state_iterator->second) {
      case TargetCollectionState::UNCONFIGURED:
        return ASTL_STATUS_COLLECTION_NOT_CONFIGURED;
      case TargetCollectionState::CONFIGURED:
        state_iterator->second = TargetCollectionState::STARTING;
        break;  // allowed
      case TargetCollectionState::STARTING:
        return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
      case TargetCollectionState::STARTED:
        return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
      case TargetCollectionState::PAUSED:
        return ASTL_STATUS_INVALID_STATE_TRANSITION;  // must call ResumeCollection
      case TargetCollectionState::STOPPED:
        return ASTL_STATUS_INVALID_STATE_TRANSITION;
    }
  }
  std::unique_lock lock{_raw_samples_mtx};
  _raw_samples[target].clear();
  lock.unlock();  // in case _collector_manager runs operations on Start that try to sink samples to us

  // Take per-operation clock correlation snapshots before StartOnTarget so that the native clock
  // anchor is as close in time as possible to when the first samples will be produced.
  // Each OperationId maps to its own (CLOCK_MONOTONIC_RAW, native-clock) pair to handle the case
  // where different SCMI data events reference independent hardware timers.
  auto clock_correlations = _collector_manager->GetNativeClockSnapshot(target);
  if (!clock_correlations) {
    return clock_correlations.error();
  }
  _metric_manager->SetClockCorrelations(*clock_correlations);

  const auto start_status = _collector_manager->StartOnTarget(target);
  if (start_status != ASTL_STATUS_SUCCESS) {
    // Re-acquire lock and guard against a concurrent StopCollection that may have already
    // transitioned us out of STARTING while StartOnTarget() was in flight.
    std::lock_guard state_lock(_collection_state_mutex);
    auto            it = _target_collection_states.find(target);
    if (it != _target_collection_states.end() && it->second == TargetCollectionState::STARTING) {
      it->second = TargetCollectionState::CONFIGURED;
    }
    return start_status;
  }

  if (!start_paused) {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            it = _target_collection_states.find(target);
    if (it != _target_collection_states.end() && it->second == TargetCollectionState::STARTING) {
      it->second = TargetCollectionState::STARTED;
    }
    return ASTL_STATUS_SUCCESS;
  }

  auto pause_status = _collector_manager->PauseOnTarget(target);
  if (pause_status == ASTL_STATUS_SUCCESS) {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            it = _target_collection_states.find(target);
    if (it != _target_collection_states.end() && it->second == TargetCollectionState::STARTING) {
      it->second = TargetCollectionState::PAUSED;
    }
    _target_pause_timestamps[target] = std::chrono::steady_clock::now();
    return ASTL_STATUS_SUCCESS;
  }

  const auto rollback_status = _collector_manager->StopOnTarget(target);
  {
    std::lock_guard raw_samples_lock(_raw_samples_mtx);
    _raw_samples[target].clear();
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            it = _target_collection_states.find(target);
    if (it != _target_collection_states.end() && it->second == TargetCollectionState::STARTING) {
      it->second =
          (rollback_status == ASTL_STATUS_SUCCESS) ? TargetCollectionState::CONFIGURED : TargetCollectionState::STARTED;
    }
  }
  if (rollback_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to roll back collection start after pause request failed: {}",
                   astlStatusString(rollback_status));
    return rollback_status;
  }
  if (pause_status == ASTL_STATUS_NOT_IMPLEMENTED) {
    return ASTL_STATUS_PAUSE_UNSUPPORTED;
  }
  return pause_status;
}

auto Orchestrator::StartCollection(const ITarget *target) -> astl_status_code {
  return StartCollectionImpl(target, false);
}

auto Orchestrator::StartCollectionPaused(const ITarget *target) -> astl_status_code {
  return StartCollectionImpl(target, true);
}

auto Orchestrator::RollbackStartedCollectionToConfigured(const ITarget *target) -> astl_status_code {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::RollbackStartedCollectionToConfigured called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto stop_status = _collector_manager->StopOnTarget(target);
  if (stop_status != ASTL_STATUS_SUCCESS) {
    return stop_status;
  }

  const auto reset_status = ResetTargetCollectionArtifacts(target);
  if (reset_status != ASTL_STATUS_SUCCESS) {
    return reset_status;
  }
  ResetFinalOutputEmissionState();

  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::CONFIGURED;
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ReadImmediate(const ITarget *target) -> astl_status_code {
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::ReadImmediate called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return _collector_manager->ReadImmediateOnTarget(target);
}

auto Orchestrator::PauseCollection(const ITarget *target) -> astl_status_code {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  astl_status_code collector_status{ASTL_STATUS_SUCCESS};
  {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            state_iterator = _target_collection_states.find(target);
    if (state_iterator == _target_collection_states.end()) {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
    if (state_iterator->second == TargetCollectionState::PAUSED) {
      return ASTL_STATUS_COLLECTION_ALREADY_PAUSED;
    }
    if (state_iterator->second != TargetCollectionState::STARTED) {
      return ASTL_STATUS_COLLECTION_NOT_RUNNING;
    }
  }
  collector_status = _collector_manager->PauseOnTarget(target);
  if (collector_status != ASTL_STATUS_SUCCESS) {
    if (collector_status == ASTL_STATUS_NOT_IMPLEMENTED) {
      return ASTL_STATUS_PAUSE_UNSUPPORTED;
    }
    return collector_status;
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::PAUSED;
    _target_pause_timestamps[target]  = std::chrono::steady_clock::now();
  }
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ResumeCollection(const ITarget *target) -> astl_status_code {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  astl_status_code collector_status{ASTL_STATUS_SUCCESS};
  {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            state_iterator = _target_collection_states.find(target);
    if (state_iterator == _target_collection_states.end()) {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
    if (state_iterator->second == TargetCollectionState::STARTED) {
      return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
    }
    if (state_iterator->second != TargetCollectionState::PAUSED) {
      return ASTL_STATUS_COLLECTION_NOT_PAUSED;
    }
  }
  collector_status = _collector_manager->ResumeOnTarget(target);
  if (collector_status != ASTL_STATUS_SUCCESS) {
    if (collector_status == ASTL_STATUS_NOT_IMPLEMENTED) {
      return ASTL_STATUS_RESUME_UNSUPPORTED;
    }
    return collector_status;
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::STARTED;
    _target_resume_timestamps[target] = std::chrono::steady_clock::now();
  }
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::StopCollection(const ITarget *target) -> astl_status_code {
  if (!_metric_manager) {
    ASTL_LOG_ERROR("null _metric_manager in Orchestrator::StopCollection");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_collector_manager) {
    ASTL_LOG_ERROR("null _collector_manager in Orchestrator::StopCollection");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }

  {
    std::lock_guard state_lock(_collection_state_mutex);
    auto            state_iterator = _target_collection_states.find(target);
    if (state_iterator == _target_collection_states.end()) {
      return ASTL_STATUS_INVALID_TARGET_HANDLE;
    }
    // Reject stop requests while a start attempt is still in flight to keep
    // collector behavior and state transitions consistent (disallow STOP during STARTING).
    if (state_iterator->second == TargetCollectionState::STARTING) {
      return ASTL_STATUS_INVALID_STATE_TRANSITION;
    }
  }

  auto status = _collector_manager->StopOnTarget(target);  // finalize collector state
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  {
    std::lock_guard lock{_raw_samples_mtx};

    // Serialize any remaining in-memory samples for this target into the batch file, then clear.
    // Do not hold _raw_samples_mtx while emitting final reports; those paths may rebuild processed
    // samples and otherwise do non-trivial work during stop/finalize.
    auto iter = _raw_samples.find(target);
    if (iter != _raw_samples.end() && !iter->second.empty()) {
      auto res = astl::ProtobufSerDes::SerializeCurrentBatch(GetStableTargetKey(*target), iter->second, _cache_dir);
      if (res != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to serialize remaining samples for {}", target->Name());
        return res;
      }
      iter->second.clear();
    }
  }

  // if we've now stopped collection on all the targets, finalize metric processing and output
  if (!_collector_manager->IsAnyTargetBeingCollected()) {
    // Emit Perfetto trace (if requested) after metrics are summarized (processed samples complete)
    EmitPerfettoTraceIfRequested();

    // Emit Interval CSV trace (if requested) after metrics are summarized (processed samples complete)
    EmitIntervalCsvIfRequested();

    // Emit Summary CSV (if requested) after metrics are summarized (processed samples complete)
    EmitSummaryCsvIfRequested();
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::STOPPED;
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::EmitPerfettoTraceIfRequested() -> void {
  if (!TryBeginFinalOutputEmission(_perfetto_emission_state)) {
    return;
  }

  std::string perfetto_path = astl::GetEnvVar(astl::EnvVar::ASTL_OUTPUT_PERFETTO);
  if (perfetto_path.empty()) {
    FinishFinalOutputEmission(_perfetto_emission_state, false);
    return;  // no-op if unset
  }
  EnsureFinalEmissionProcessedSamplesRebuilt();
  // Acquire processed samples snapshot under lock to avoid race with late sample insertion.
  std::lock_guard processed_lock{_processed_samples_mtx};
  if (_processed_samples.empty()) {
    ASTL_LOG_INFO("Perfetto trace requested but no processed samples available; emitting empty trace");
  }
  if (!_output_manager) {
    ASTL_LOG_ERROR("EmitPerfettoTraceIfRequested: OutputManager unavailable");
    FinishFinalOutputEmission(_perfetto_emission_state, false);
    return;
  }
  // Dispatch all processed samples via OutputManager using PERFETTO type.
  astl_status_code status =
      _output_manager->OutputProcessedSamples(_processed_samples, OutputType::PERFETTO, nullptr, nullptr);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("EmitPerfettoTraceIfRequested: failed to emit Perfetto trace (status={})", status);
    FinishFinalOutputEmission(_perfetto_emission_state, false);
    return;
  }
  FinishFinalOutputEmission(_perfetto_emission_state, true);
  ASTL_LOG_INFO("Perfetto trace emission completed (env path='{}')", perfetto_path);
}

auto Orchestrator::EmitIntervalCsvIfRequested() -> void {
  if (!TryBeginFinalOutputEmission(_intervalcsv_emission_state)) {
    return;
  }

  std::string csv_path = astl::GetEnvVar(astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV);
  if (csv_path.empty()) {
    FinishFinalOutputEmission(_intervalcsv_emission_state, false);
    return;  // nothing to do
  }
  EnsureFinalEmissionProcessedSamplesRebuilt();

  std::lock_guard processed_lock{_processed_samples_mtx};
  if (!_output_manager) {
    ASTL_LOG_ERROR("EmitIntervalCsvIfRequested: OutputManager unavailable");
    FinishFinalOutputEmission(_intervalcsv_emission_state, false);
    return;
  }
  astl_status_code status =
      _output_manager->OutputProcessedSamples(_processed_samples, OutputType::INTERVAL_CSV, nullptr, nullptr);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("EmitIntervalCsvIfRequested: failed to emit interval CSV (status={})", status);
    FinishFinalOutputEmission(_intervalcsv_emission_state, false);
    return;
  }
  FinishFinalOutputEmission(_intervalcsv_emission_state, true);
  ASTL_LOG_INFO("Interval CSV emission completed (env path='{}')", csv_path);
}

auto Orchestrator::EmitSummaryCsvIfRequested() -> void {
  std::string csv_path = astl::GetEnvVar(astl::EnvVar::ASTL_OUTPUT_SUMMARY_CSV);
  if (csv_path.empty()) {
    return;  // no-op if unset
  }

  EnsureFinalEmissionProcessedSamplesRebuilt();

  // Acquire processed samples snapshot under lock to avoid race with late sample insertion.
  std::lock_guard processed_lock{_processed_samples_mtx};
  if (_processed_samples.empty()) {
    ASTL_LOG_INFO("Summary CSV requested but no processed samples available; emitting empty file");
  }
  if (!_output_manager) {
    ASTL_LOG_ERROR("EmitSummaryCsvIfRequested: OutputManager unavailable");
    return;
  }
  // Dispatch all processed samples via OutputManager using SUMMARY_CSV type.
  astl_status_code status =
      _output_manager->OutputProcessedSamples(_processed_samples, OutputType::SUMMARY_CSV, nullptr, nullptr);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("EmitSummaryCsvIfRequested: failed to emit summary CSV (status={})", status);
    return;
  }
  ASTL_LOG_INFO("Summary CSV emission completed (env path='{}')", csv_path);
}

auto Orchestrator::EnsureFinalEmissionProcessedSamplesRebuilt() const -> void {
  if (_final_rebuild_attempted.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  RebuildProcessedSamplesForAllTargets();
}

auto Orchestrator::RebuildProcessedSamplesForAllTargets() const -> void {
  // Ensure _processed_samples is fully populated before emission.
  // A single GetProcessedMetricSamples() call can rebuild all metrics for a target,
  // so we attempt at most once per target to avoid repeated rebuild work.
  if (!_metric_manager || !_topology_manager) {
    return;
  }
  if (_cache_dir.empty()) {
    return;
  }

  for (const auto &target : _topology_manager->GetTargets()) {
    // Rebuild is only meaningful if a cached raw-sample batch exists for this target.
    // This avoids expensive and unnecessary metric-manager probing in no-samples paths.
    const auto cache_file_path = _cache_dir / (GetStableTargetKey(*target) + kAstlFileExtension);
    if (!fs::exists(cache_file_path)) {
      continue;
    }

    auto handles_or_err = _metric_manager->GetAvailableMetrics(target.get());
    if (!handles_or_err.has_value()) {
      continue;
    }

    for (const auto &handle : handles_or_err.value()) {
      auto metric_or_err = _metric_manager->GetMetricOnTarget(handle, target.get());
      if (!metric_or_err.has_value()) {
        continue;
      }
      // Side-effect: may lazily rebuild and populate all processed samples for this target.
      (void)GetProcessedMetricSamples(metric_or_err.value(), target.get());
      break;
    }
  }
}

auto Orchestrator::SinkRawSamples(const ITarget *target, std::span<RawSampledData> raw_samples) -> astl_status_code {
  if (!target) {
    ASTL_LOG_ERROR("Orchestrator::SinkSamples called with null target");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  /* Get the target name - just for logging */
  astl_target_props_t properties;
  auto                result = target->GetProperties(&properties);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }
  ASTL_LOG_DEBUG("Received {} samples for target {}", raw_samples.size(), properties.name);
  const auto stable_target_key = GetStableTargetKey(*target);

  if (!raw_samples.empty()) [[likely]] {
    std::vector<RawSampledData> batch_samples{};
    std::scoped_lock            lock{_raw_samples_mtx};
    auto                       &target_samples_vec = _raw_samples[target];

    if (target_samples_vec.size() >= kMaxSamplesPerBatch) {
      ASTL_LOG_DEBUG("target_sample_vec size {} exceeded max batch size {}, serializing current batch",
                     target_samples_vec.size(), kMaxSamplesPerBatch);
      batch_samples.swap(target_samples_vec);
    }

    // Bulk reserve once based on total required size rather than per-sample growth decisions.
    // We keep the 1.5x + bias heuristic but ensure we always meet the exact required size.
    const auto required_size = target_samples_vec.size() + raw_samples.size();
    if (target_samples_vec.capacity() < required_size) {
      auto current       = target_samples_vec.size();
      auto heuristic_cap = static_cast<size_t>((current * kRawSampleGrowthNumerator / kRawSampleGrowthDenominator) +
                                               kRawSampleGrowthBias);
      // Guarantee capacity is at least the immediate requirement (heuristic may be smaller when current==0).
      auto new_cap = std::max(required_size, heuristic_cap);
      target_samples_vec.reserve(new_cap);
    }

    // Single bulk insert instead of repeated push_back calls.
    target_samples_vec.insert(target_samples_vec.end(), raw_samples.begin(), raw_samples.end());

    // Preserve per-sample debug logging (separate pass keeps insertion branch-predictable / cache-friendly).
    for (const auto &sample : raw_samples) {
      auto timestamp_ns = sample.raw_tick;
      auto value        = sample.value;
      ASTL_LOG_DEBUG("Raw Sample - raw_tick: {}, value: {}", timestamp_ns, value);
    }

    if (!batch_samples.empty()) {
      auto res = astl::ProtobufSerDes::SerializeCurrentBatch(stable_target_key, batch_samples, _cache_dir);
      if (res != ASTL_STATUS_SUCCESS) {
        return res;
      }
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::SinkProcessedSamples(const ITarget *target, const IMetric *metric,
                                        std::span<const ProcessedSampledData> processed_samples) -> astl_status_code {
  if (!target) {
    ASTL_LOG_ERROR("Orchestrator::SinkProcessedSamples called with null target");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  if (!metric) {
    ASTL_LOG_ERROR("Orchestrator::SinkProcessedSamples called with null metric");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  ASTL_LOG_DEBUG("Received {} samples for metric {} on target {}", processed_samples.size(), metric->Name(),
                 target->Name());

  {
    std::scoped_lock lock{_processed_samples_mtx};
    auto            &metric_map = _processed_samples[target];
    auto            &vec        = metric_map[metric];
    // Batch reserve up-front to avoid repeated reallocations when adding N new processed samples.
    if (vec.capacity() < vec.size() + processed_samples.size()) {
      auto required = vec.size() + processed_samples.size();
      // Same growth heuristic (1.5x + bias) abstracted via named constants.
      auto new_cap = static_cast<size_t>((required * kProcSampleGrowthNumerator / kProcSampleGrowthDenominator) +
                                         kProcSampleGrowthBias);
      vec.reserve(new_cap);
    }
    for (const auto &sample : processed_samples) {
      vec.push_back(sample);
      auto timestamp_ns = sample.timestamp.time_since_epoch().count();
      auto value        = sample.value;
      ASTL_LOG_DEBUG("Processed Sample - timestamp (ns since epoch): {}, value: {}", timestamp_ns, value);
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::GetPauseMarkersSnapshot() const -> PauseMarkersMap {
  // Resolve each target's pause-event metric directly from MetricManager — no opaque handle
  std::unordered_map<const ITarget *, const IMetric *> target_to_pause_metric;
  {
    std::lock_guard state_lock{_collection_state_mutex};
    for (const auto &[target, collection_state] : _target_collection_states) {
      static_cast<void>(collection_state);
      if (const auto *pause_metric = _metric_manager->GetPauseResumeEventMetricOnTarget(target);
          pause_metric != nullptr) {
        target_to_pause_metric[target] = pause_metric;
      }
    }
  }

  // Phase 2: snapshot timestamps from _processed_samples under the mutex.
  PauseMarkersMap result;
  std::lock_guard lock{_processed_samples_mtx};
  for (const auto &[target, pause_metric] : target_to_pause_metric) {
    const auto target_it = _processed_samples.find(target);
    if (target_it == _processed_samples.end()) {
      continue;
    }
    const auto metric_it = target_it->second.find(pause_metric);
    if (metric_it == target_it->second.end()) {
      continue;
    }
    auto &timestamps = result[target];
    timestamps.reserve(metric_it->second.size());
    for (const auto &sample : metric_it->second) {
      // Filter to pause events only (value=0).  Resume events (value=1) share the same
      // EventMetric but must not be passed to ComputeTimeWeightedAverage as pause boundaries.
      const auto *val = std::get_if<uint64_t>(&sample.value.value);
      if (val != nullptr && *val == 0) {
        timestamps.push_back(sample.timestamp);
      }
    }
  }
  return result;
}

auto Orchestrator::RegisterPauseResumeEventMetricForTarget(const ITarget *target) -> astl_status_code {
  if (!target || !_metric_manager) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  // Already registered for this target — MetricManager will reject the duplicate metric id
  // gracefully, but we can skip work by checking the dedicated map.
  if (_metric_manager->GetPauseResumeEventMetricOnTarget(target) != nullptr) {
    return ASTL_STATUS_SUCCESS;
  }

  // Use a target-scoped ID to avoid conflicts when multiple targets are configured.
  const std::string metric_name = std::string{"astl_pause_events."} + target->Name();
  auto              cfg = std::make_unique<MetricConfig>(metric_name, "Pause events emitted during collection pauses",
                                                         ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                                         ASTL_METRIC_EVENT, CollectorType::ASTL_NATIVE, NullOperationBuilder{});

  const auto status = _metric_manager->RegisterMetric(std::move(cfg), {target});
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Orchestrator: failed to register pause-event metric for target '{}': {}", target->Name(),
                   astlStatusString(status));
    return status;
  }

  // MetricManager::RegisterMetric stores the IMetric* in _target_to_pause_resume_event_metric;
  // no need to locate the handle — GetPauseResumeEventMetricOnTarget provides direct access.
  ASTL_LOG_DEBUG("Orchestrator: registered pause-event metric '{}' for target '{}'", metric_name, target->Name());
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Retrieve the collected samples for the given target and metric,
 *        or an error if the target+metric combination isn't valid.
 *
 * If we don't have processed samples in-memory yet, attempt to rebuild them
 * by deserializing raw samples from a temporary file and re-processing.
 */
auto Orchestrator::GetProcessedMetricSamples(const IMetric *metric, const ITarget *target) const
    -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code> {
  if (!metric || !target) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (!_metric_manager) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto lookup = [&]() -> std::optional<std::span<const astl::ProcessedSampledData>> {
    std::scoped_lock lock{_processed_samples_mtx};

    const auto target_it = _processed_samples.find(target);
    if (target_it == _processed_samples.end()) {
      return std::nullopt;
    }

    const auto &metric_map = target_it->second;
    const auto  metric_it  = metric_map.find(metric);
    if (metric_it == metric_map.end()) {
      return std::nullopt;
    }

    return std::span<const astl::ProcessedSampledData>(metric_it->second);
  };

  // 1) Fast path
  if (auto samples = lookup()) {
    return *samples;
  }

  // 2) Rebuild from tmp/<target>.astl (if it exists)
  // TODO(ASTL-224): Support batch processing. Currently we just hardcode tmp/ as the location.
  const fs::path file_path = _cache_dir / (GetStableTargetKey(*target) + kAstlFileExtension);

  if (!fs::exists(fs::path(_cache_dir))) {
    ASTL_LOG_DEBUG("No cache directory exists; skipping raw sample deserialization for target {}", target->Name());
    return {};
  }

  if (!fs::exists(file_path)) {
    ASTL_LOG_INFO("No cached raw sample file exists for target {}; returning no samples", target->Name());
    return {};
  }

  std::ifstream file_stream(file_path, std::ios::binary);
  if (!file_stream) {
    ASTL_LOG_ERROR("Failed to open {} for reading", file_path.string());
    return {};
  }

  auto raw = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(file_stream);
  if (!raw) {
    ASTL_LOG_ERROR("Failed to deserialize samples from {}: {}", file_path.string(), astlStatusString(raw.error()));
    return std::unexpected(raw.error());
  }

  if (!raw->empty()) {
    // Rebuild processed samples from raw cache using a clean per-target metric state.
    // This avoids replaying into delta/rate metrics that still hold an old "previous sample".
    auto reset_status = _metric_manager->ResetMetricsOnTarget(target);
    if (reset_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(reset_status);
    }

    {
      std::scoped_lock lock{_processed_samples_mtx};
      _processed_samples.erase(target);
    }

    // ProcessRawSamples sinks results back via SinkProcessedSamples() - don't hold the mutex here.
    RawSamplesMap raw_samples{
        {target, std::move(*raw)}
    };

    if (auto process_status = _metric_manager->ProcessRawSamples(raw_samples); process_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(process_status);
    }

    if (auto summarize_status = _metric_manager->SummarizeMetrics(); summarize_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(summarize_status);
    }
  }

  // 3) Retry
  if (auto samples = lookup()) {
    return *samples;
  }

  ASTL_LOG_WARNING("GetProcessedMetricSamples: No samples found for metric {} on target {}", metric->Name(),
                   target->Name());
  return {};  // not found
}

auto Orchestrator::GetTargetCollectionState(const ITarget *target) const
    -> std::expected<TargetCollectionState, astl_status_code> {
  if (!target) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  // Verify target is part of current topology to avoid stale pointer queries after SetTargets.
  const auto &targets = _topology_manager->GetTargets();
  auto        index   = std::find_if(std::begin(targets), std::end(targets),
                                     [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
  std::lock_guard state_lock(_collection_state_mutex);
  auto            state_iterator = _target_collection_states.find(target);
  if (state_iterator == _target_collection_states.end()) {
    return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
  return state_iterator->second;
}

auto Orchestrator::GetAllTargetCollectionStates() const -> std::unordered_map<const ITarget *, TargetCollectionState> {
  std::lock_guard state_lock(_collection_state_mutex);
  return _target_collection_states;  // copy
}

auto Orchestrator::TargetCollectionStateToString(TargetCollectionState state) -> std::string_view {
  // magic_enum provides compile-time reflection for enum names without maintaining a manual mapping array.
  // This reduces maintenance risk when adding states and avoids switch/default boilerplate.
  auto state_name_view = magic_enum::enum_name(state);
  if (state_name_view.empty()) {
    return std::string_view{"UNKNOWN_STATE"};
  }
  return state_name_view;  // string_view to static storage provided by magic_enum
}

auto Orchestrator::SaveToFile(std::filesystem::path file_path) -> astl_status_code {
  auto status = SaveStateToCacheDir();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  const auto &orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto &orchestrator   = orchestrator_or_error->get();
  const auto  cache_dir_path = orchestrator->_cache_dir;

  status = mz::ZipDirectory(cache_dir_path, file_path);
  return status;
}

auto Orchestrator::SaveStateToCacheDir() -> astl_status_code {
  const auto &orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto &orchestrator   = orchestrator_or_error->get();
  const auto  cache_dir_path = orchestrator->_cache_dir;

  // create directory if it does not exist
  if (!fs::exists(cache_dir_path)) {
    std::error_code err_code;
    fs::create_directories(cache_dir_path, err_code);
    if (err_code) {
      ASTL_LOG_ERROR("Failed to create directory {}: {}", cache_dir_path.string(), err_code.message());
      return ASTL_STATUS_INTERNAL_ERROR;
    }
  }

  {
    fs::path      topology_manager_file_path = cache_dir_path / kTopologyManagerFileName;
    std::ofstream topology_file{topology_manager_file_path, std::ios::binary | std::ios::out};
    auto          status = ProtobufSerDes::Serialize(*orchestrator->_topology_manager, topology_file);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to serialize topology to {}: {}", topology_manager_file_path.string(),
                     astlStatusString(status));
      return status;
    }

    fs::path      metric_manager_file_path = cache_dir_path / kMetricManagerFileName;
    std::ofstream metric_manager_file{metric_manager_file_path, std::ios::binary | std::ios::out};
    status = ProtobufSerDes::Serialize(*orchestrator->_metric_manager, metric_manager_file);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to serialize metric manager to {}: {}", metric_manager_file_path.string(),
                     astlStatusString(status));
      return status;
    }
  }

  auto pool_status = SaveStringPoolToCacheDir(cache_dir_path);
  if (pool_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to serialize string pool to {}: {}", cache_dir_path.string(), astlStatusString(pool_status));
    return pool_status;
  }

  auto system_info_status = SavePlatformInfoToCacheDir(cache_dir_path);
  if (system_info_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to serialize platform info to cache dir {}: {}", cache_dir_path.string(),
                   astlStatusString(system_info_status));
    return system_info_status;
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::LoadFromFile(fs::path file_path, fs::path cache_dir_path) -> astl_status_code {
  if (instance_) {
    ASTL_LOG_WARNING("Orchestrator instance already initialized - cannot LoadFromFile");
    return ASTL_STATUS_SUCCESS;
  }

  auto status = mz::UnzipDirectory(file_path.string(), cache_dir_path.string());

  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to unzip ASTL file {}: {}", file_path.string(), astlStatusString(status));
    return status;
  }

  status = LoadStringPoolFromCacheDir(cache_dir_path);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to deserialize string pool from cache dir {}: {}", cache_dir_path.string(),
                   astlStatusString(status));
    return status;
  }

  status = LoadPlatformInfoFromCacheDir(cache_dir_path);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to deserialize platform info from cache dir {}: {}", cache_dir_path.string(),
                   astlStatusString(status));
    return status;
  }

  return ASTL_STATUS_SUCCESS;
}
}  // namespace astl
