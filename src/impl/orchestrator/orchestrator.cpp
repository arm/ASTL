// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "orchestrator/orchestrator.hpp"

#include <algorithm>  // for std::max used in bulk reserve heuristic
#include <filesystem>
#include <fstream>
#include <functional>  // for std::reference_wrapper in expected return types
#include <iterator>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_defines.hpp"
#include "astl_internal_status.hpp"
#include "astl_logger.hpp"
#include "astl_magic_enum.hpp"
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

auto GetSampleOperationIds(const CollectionOperations &operations) -> std::vector<OperationId> {
  std::vector<OperationId> operation_ids;
  operation_ids.reserve(operations.operationsOnSample.size());
  for (const auto &operation : operations.operationsOnSample) {
    if (operation) {
      operation_ids.push_back(operation->GetId());
    }
  }
  return operation_ids;
}
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
  {
    std::lock_guard raw_samples_lock(_raw_samples_mtx);
    _no_cache_targets.clear();
  }
  _metric_manager->RemoveAllMetrics();
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ConfigureCounterCollection(const ITarget *target, const astl_collection_params_t *collection_params,
                                              std::span<const astl_counter_handle_t> counters) -> astl_status_code {
  std::lock_guard                              configure_lock(_configure_mutex);
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
  auto reset_status = ResetOperationIdsForCleanConfigure(target);
  if (reset_status != ASTL_STATUS_SUCCESS) {
    return reset_status;
  }
  auto operations = _metric_manager->GetCounterRequiredOperations(counters, target);
  if (!operations) {
    return IsInternalStatus(operations.error()) ? operations.error() : ASTL_STATUS_INTERNAL_ERROR;
  }
  const auto       active_operation_ids = GetSampleOperationIds(operations.value());
  astl_status_code status =
      _collector_manager->ConfigureCollectionOnTarget(target, *collection_params, std::move(operations.value()));
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to configure collection on target: {}", astlStatusString(status));
    return status;
  }
  status = _metric_manager->ClearStaleOperationStateForTarget(target, active_operation_ids);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to prune stale operation state on target {}: {}", target->Name(), astlStatusString(status));
    return status;
  }
  status = ResetTargetCollectionArtifacts(target);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to reset collection artifacts on target {}: {}", target->Name(), astlStatusString(status));
    return status;
  }
  ResetFinalOutputEmissionState();
  {
    std::lock_guard raw_samples_lock(_raw_samples_mtx);
    if ((collection_params->flags & ASTL_NO_CACHING) != 0U) {
      _no_cache_targets.insert(target);
    } else {
      _no_cache_targets.erase(target);
    }
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::CONFIGURED;
  }

  return status;
}

auto Orchestrator::ConfigureMetricCollection(const ITarget *target, const astl_collection_params_t *collection_params,
                                             std::span<const astl_metric_handle_t> metrics) -> astl_status_code {
  std::lock_guard                              configure_lock(_configure_mutex);
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
  auto reset_status = ResetOperationIdsForCleanConfigure(target);
  if (reset_status != ASTL_STATUS_SUCCESS) {
    return reset_status;
  }
  auto operations = _metric_manager->GetRequiredOperations(metrics, target);
  if (!operations) {
    return IsInternalStatus(operations.error()) ? operations.error() : ASTL_STATUS_INTERNAL_ERROR;
  }
  const auto       active_operation_ids = GetSampleOperationIds(operations.value());
  astl_status_code status =
      _collector_manager->ConfigureCollectionOnTarget(target, *collection_params, std::move(operations.value()));
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to configure collection on target: {}", astlStatusString(status));
    return status;
  }
  status = _metric_manager->ClearStaleOperationStateForTarget(target, active_operation_ids);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to prune stale operation state on target {}: {}", target->Name(), astlStatusString(status));
    return status;
  }
  status = ResetTargetCollectionArtifacts(target);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to reset collection artifacts on target {}: {}", target->Name(), astlStatusString(status));
    return status;
  }
  ResetFinalOutputEmissionState();
  {
    std::lock_guard raw_samples_lock(_raw_samples_mtx);
    if ((collection_params->flags & ASTL_NO_CACHING) != 0U) {
      _no_cache_targets.insert(target);
    } else {
      _no_cache_targets.erase(target);
    }
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_collection_states[target] = TargetCollectionState::CONFIGURED;
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

auto Orchestrator::ResetAllCollectionArtifacts() -> astl_status_code {
  for (const auto &target : _topology_manager->GetTargets()) {
    const auto status = ResetTargetCollectionArtifacts(target.get());
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }

  {
    std::lock_guard raw_lock(_raw_samples_mtx);
    _raw_samples.clear();
    _no_cache_targets.clear();
  }
  {
    std::lock_guard processed_lock(_processed_samples_mtx);
    _processed_samples.clear();
  }
  {
    std::lock_guard state_lock(_collection_state_mutex);
    _target_pause_timestamps.clear();
    _target_resume_timestamps.clear();
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ResetOperationIdsForCleanConfigure(const ITarget *target) -> astl_status_code {
  if (_clean_configure_reset_pending) {
    _clean_configure_reset_pending = false;
    return ASTL_STATUS_SUCCESS;
  }

  bool should_reset = true;
  {
    std::lock_guard state_lock(_collection_state_mutex);
    for (const auto &[configured_target, state] : _target_collection_states) {
      if (state == TargetCollectionState::STARTING || state == TargetCollectionState::STARTED ||
          state == TargetCollectionState::PAUSED) {
        return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
      }
      if (state == TargetCollectionState::CONFIGURED && configured_target != target) {
        should_reset = false;
      }
    }
  }

  if (!should_reset) {
    return ASTL_STATUS_SUCCESS;
  }

  const auto reset_status        = ResetCollectionStateForCleanConfigureLocked();
  _clean_configure_reset_pending = false;
  return reset_status;
}

auto Orchestrator::ResetCollectionStateForCleanConfigure() -> astl_status_code {
  std::lock_guard configure_lock(_configure_mutex);
  {
    std::lock_guard state_lock(_collection_state_mutex);
    for (const auto &[target, state] : _target_collection_states) {
      (void)target;
      if (state == TargetCollectionState::STARTING || state == TargetCollectionState::STARTED ||
          state == TargetCollectionState::PAUSED) {
        return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
      }
    }
  }

  const auto reset_status        = ResetCollectionStateForCleanConfigureLocked();
  _clean_configure_reset_pending = reset_status == ASTL_STATUS_SUCCESS;
  return reset_status;
}

auto Orchestrator::ResetCollectionStateForCleanConfigureLocked() -> astl_status_code {
  const auto collector_reset_status = _collector_manager->ClearConfiguredCollections();
  if (collector_reset_status != ASTL_STATUS_SUCCESS) {
    return collector_reset_status;
  }
  MarkAllTargetsUnconfigured();
  const auto reset_status = ResetAllCollectionArtifacts();
  if (reset_status != ASTL_STATUS_SUCCESS) {
    return reset_status;
  }
  _metric_manager->ClearCollectionOperationState();
  Operation::ResetOperationIdAllocator();
  ResetFinalOutputEmissionState();
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::MarkAllTargetsUnconfigured() -> void {
  std::lock_guard state_lock(_collection_state_mutex);
  for (auto &[target, state] : _target_collection_states) {
    (void)target;
    state = TargetCollectionState::UNCONFIGURED;
  }
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
    // A pause event is about to be recorded; ensure the lifecycle-event metric exists for it.
    EnsureLifecycleEventMetricForTarget(target);
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
  if (pause_status == astl::kInternalNotImplemented) {
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
  if (!_metric_manager) {
    ASTL_LOG_ERROR("Orchestrator::ReadImmediate called with null MetricManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto should_capture_clock_correlation = false;
  {
    std::lock_guard state_lock(_collection_state_mutex);
    const auto      state_iterator   = _target_collection_states.find(target);
    should_capture_clock_correlation = state_iterator != _target_collection_states.end() &&
                                       state_iterator->second == TargetCollectionState::CONFIGURED;
  }

  if (should_capture_clock_correlation) {
    auto clock_correlations = _collector_manager->GetNativeClockSnapshot(target);
    if (!clock_correlations) {
      return clock_correlations.error();
    }
    _metric_manager->SetClockCorrelations(*clock_correlations);
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
  // A pause event is about to be recorded; ensure the lifecycle-event metric exists for it.
  EnsureLifecycleEventMetricForTarget(target);
  collector_status = _collector_manager->PauseOnTarget(target);
  if (collector_status != ASTL_STATUS_SUCCESS) {
    if (collector_status == astl::kInternalNotImplemented) {
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
  // A resume event is about to be recorded; ensure the lifecycle-event metric exists for it.
  EnsureLifecycleEventMetricForTarget(target);
  collector_status = _collector_manager->ResumeOnTarget(target);
  if (collector_status != ASTL_STATUS_SUCCESS) {
    if (collector_status == astl::kInternalNotImplemented) {
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
    std::vector<RawSampledData> samples_to_process{raw_samples.begin(), raw_samples.end()};
    std::vector<RawSampledData> batch_samples{};
    bool                        cache_suppressed = false;
    {
      std::scoped_lock lock{_raw_samples_mtx};
      cache_suppressed = _no_cache_targets.contains(target);
    }
    if (cache_suppressed) {
      RawSamplesMap uncached_samples{
          {target, std::move(samples_to_process)}
      };
      return _metric_manager->ProcessRawSamples(uncached_samples);
    }

    {
      std::scoped_lock lock{_raw_samples_mtx};
      auto            &target_samples_vec = _raw_samples[target];

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
    }

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

auto Orchestrator::ConsumeProcessedMetricSamplesIfUncached(const IMetric *metric, const ITarget *target) -> void {
  if (!metric || !target) {
    return;
  }
  {
    std::scoped_lock raw_lock{_raw_samples_mtx};
    if (!_no_cache_targets.contains(target)) {
      return;
    }
  }

  std::scoped_lock processed_lock{_processed_samples_mtx};
  auto             target_it = _processed_samples.find(target);
  if (target_it == _processed_samples.end()) {
    return;
  }
  target_it->second.erase(metric);
  if (target_it->second.empty()) {
    _processed_samples.erase(target_it);
  }
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
      if (const auto *pause_metric = _metric_manager->GetLifecycleEventMetricOnTarget(target);
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

auto Orchestrator::RegisterLifecycleEventMetricForTarget(const ITarget *target) -> astl_status_code {
  if (!target || !_metric_manager) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  // Already registered for this target — MetricManager will reject the duplicate metric id
  // gracefully, but we can skip work by checking the dedicated map.
  if (_metric_manager->GetLifecycleEventMetricOnTarget(target) != nullptr) {
    return ASTL_STATUS_SUCCESS;
  }

  // Use a target-scoped name to avoid conflicts when multiple targets are configured.
  const std::string metric_name = std::string{"astl_lifecycle_events."} + target->Name();
  auto cfg = std::make_unique<MetricConfig>(metric_name, "ASTL lifecycle events (pause, resume, crop boundary)",
                                            ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                            ASTL_METRIC_EVENT, CollectorType::ASTL_NATIVE, NullOperationBuilder{});

  const auto status = _metric_manager->RegisterMetric(std::move(cfg), {target});
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Orchestrator: failed to register lifecycle-event metric for target '{}': {}", target->Name(),
                   astlStatusString(status));
    return status;
  }

  // MetricManager::RegisterMetric stores the IMetric* in _target_to_lifecycle_event_metric;
  // no need to locate the handle — GetLifecycleEventMetricOnTarget provides direct access.
  ASTL_LOG_DEBUG("Orchestrator: registered lifecycle-event metric '{}' for target '{}'", metric_name, target->Name());
  return ASTL_STATUS_SUCCESS;
}

void Orchestrator::EnsureLifecycleEventMetricForTarget(const ITarget *target) {
  const auto status = RegisterLifecycleEventMetricForTarget(target);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_WARNING(
        "Orchestrator: lifecycle-event metric registration failed for '{}' ({}); lifecycle events for this target "
        "will not be recorded.",
        target ? target->Name() : "<null>", astlStatusString(status));
  }
}

/**
 * @brief Retrieve the collected samples for the given target and metric,
 *        if they have already been materialized in memory.
 */
auto Orchestrator::LookupProcessedMetricSamples(const IMetric *metric, const ITarget *target) const
    -> std::optional<std::span<const astl::ProcessedSampledData>> {
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
}

auto Orchestrator::ProcessRawSampleCacheStream(const ITarget *target, std::istream &file_stream) const
    -> astl_status_code {
  ProtobufSerDes::RawSampleBatchReader reader{file_stream};

  // Rebuild processed samples from raw cache using a clean per-target metric state.
  // This avoids replaying into delta/rate metrics that still hold an old "previous sample".
  const auto reset_status = _metric_manager->ResetMetricsOnTarget(target);
  if (reset_status != ASTL_STATUS_SUCCESS) {
    return reset_status;
  }

  {
    std::scoped_lock lock{_processed_samples_mtx};
    _processed_samples.erase(target);
  }

  while (true) {
    auto raw_batch_or_error = reader.ReadNext();
    if (!raw_batch_or_error) {
      return raw_batch_or_error.error();
    }
    if (raw_batch_or_error->empty()) {
      break;
    }

    // ProcessRawSamples sinks results back via SinkProcessedSamples() - don't hold the mutex here.
    RawSamplesMap raw_samples;
    raw_samples.emplace(target, std::move(*raw_batch_or_error));
    const auto replay_status = _metric_manager->ProcessRawSamples(raw_samples);
    if (replay_status != ASTL_STATUS_SUCCESS) {
      return replay_status;
    }
  }

  if (auto summarize_status = _metric_manager->SummarizeMetrics(); summarize_status != ASTL_STATUS_SUCCESS) {
    return summarize_status;
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ReplayRawSampleCacheForTarget(const ITarget *target) const -> astl_status_code {
  const fs::path file_path = _cache_dir / (GetStableTargetKey(*target) + kAstlFileExtension);

  if (!fs::exists(fs::path(_cache_dir))) {
    ASTL_LOG_DEBUG("No cache directory exists; skipping raw sample deserialization for target {}", target->Name());
    return ASTL_STATUS_SUCCESS;
  }

  if (!fs::exists(file_path)) {
    ASTL_LOG_INFO("No cached raw sample file exists for target {}; returning no samples", target->Name());
    return ASTL_STATUS_SUCCESS;
  }

  if (!fs::is_regular_file(file_path)) {
    ASTL_LOG_ERROR("Raw sample cache path {} is not a regular file", file_path.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  std::ifstream file_stream(file_path, std::ios::binary);
  if (!file_stream) {
    ASTL_LOG_ERROR("Failed to open {} for reading", file_path.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto replay_status = ProcessRawSampleCacheStream(target, file_stream);
  if (replay_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to replay raw sample batches from {}: {}", file_path.string(),
                   astlStatusString(replay_status));
  }
  return replay_status;
}

auto Orchestrator::GetProcessedMetricSamples(const IMetric *metric, const ITarget *target) const
    -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code> {
  if (!metric || !target) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (!_metric_manager) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  // 1) Fast path
  auto samples = LookupProcessedMetricSamples(metric, target);
  if (samples.has_value()) {
    return *samples;
  }

  // 2) Rebuild from the raw-sample cache file if it exists.
  const auto replay_status = ReplayRawSampleCacheForTarget(target);
  if (replay_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(replay_status);
  }

  // 3) Retry
  samples = LookupProcessedMetricSamples(metric, target);
  if (samples.has_value()) {
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

// ---------------------------------------------------------------------------
// Internal helpers for crop operations
// ---------------------------------------------------------------------------

namespace {

auto WindowEndPrecedesStart(uint64_t end_ts, uint64_t start_ts) noexcept -> bool {
  return end_ts != 0U && start_ts != 0U && end_ts < start_ts;
}

auto MaxWindowEnd(uint64_t lhs, uint64_t rhs) noexcept -> uint64_t {
  if (lhs == 0U || rhs == 0U) {
    return 0U;
  }
  return std::max(lhs, rhs);
}

auto ConsolidateCropWindows(std::span<const astl_crop_window_t> windows) -> std::vector<astl_crop_window_t> {
  if (windows.empty()) {
    return {};
  }

  std::vector<astl_crop_window_t> consolidated{windows.begin(), windows.end()};
  std::sort(consolidated.begin(), consolidated.end(), [](const auto &lhs, const auto &rhs) {
    if (lhs.start_ts == rhs.start_ts) {
      if (lhs.end_ts == 0U) {
        return false;
      }
      if (rhs.end_ts == 0U) {
        return true;
      }
      return lhs.end_ts < rhs.end_ts;
    }
    if (lhs.start_ts == 0U) {
      return true;
    }
    if (rhs.start_ts == 0U) {
      return false;
    }
    return lhs.start_ts < rhs.start_ts;
  });

  auto write_it = consolidated.begin();
  for (auto read_it = std::next(consolidated.begin()); read_it != consolidated.end(); ++read_it) {
    if (!WindowEndPrecedesStart(write_it->end_ts, read_it->start_ts)) {
      write_it->end_ts = MaxWindowEnd(write_it->end_ts, read_it->end_ts);
      continue;
    }

    ++write_it;
    if (write_it != read_it) {
      *write_it = *read_it;
    }
  }
  consolidated.erase(std::next(write_it), consolidated.end());
  return consolidated;
}

/// Returns true when @p ts_ns falls inside at least one of the @p windows.
auto IsTimestampWithinWindows(uint64_t ts_ns, std::span<const astl_crop_window_t> windows) noexcept -> bool {
  return std::any_of(windows.begin(), windows.end(), [ts_ns](const astl_crop_window_t &window) {
    const bool after_start = (window.start_ts == 0U || ts_ns >= window.start_ts);
    const bool before_end  = (window.end_ts == 0U || ts_ns <= window.end_ts);
    return after_start && before_end;
  });
}

/// Filter @p samples in-place, retaining only those within @p windows.
auto FilterProcessedSamples(std::vector<ProcessedSampledData> &samples, std::span<const astl_crop_window_t> windows)
    -> void {
  auto remove_it = std::remove_if(samples.begin(), samples.end(), [&](const ProcessedSampledData &sample) {
    const auto ts_ns = static_cast<uint64_t>(sample.timestamp.time_since_epoch().count());
    return !IsTimestampWithinWindows(ts_ns, windows);
  });
  samples.erase(remove_it, samples.end());
}

/// Filter @p raw_samples in-place using @p correlations to convert HW ticks to nanoseconds.
auto FilterRawSamples(std::vector<RawSampledData> &raw_samples, const ClockCorrelationMap &correlations,
                      std::span<const astl_crop_window_t> windows) -> void {
  auto remove_it = std::remove_if(raw_samples.begin(), raw_samples.end(), [&](const RawSampledData &sample) -> bool {
    uint64_t ts_ns = 0U;
    if (sample.IsPauseResumeMarker()) {
      // Pause/resume markers store CLOCK_MONOTONIC_RAW nanoseconds directly in raw_tick.
      ts_ns = sample.raw_tick;
    } else {
      const auto corr_it = correlations.find(sample.operation_id);
      if (corr_it == correlations.end()) {
        // No clock correlation available — conservatively keep the sample.
        return false;
      }
      ts_ns = static_cast<uint64_t>(
          NormalizeToCorrelatedRawTimestamp(sample.raw_tick, corr_it->second).time_since_epoch().count());
    }
    return !IsTimestampWithinWindows(ts_ns, windows);
  });
  raw_samples.erase(remove_it, raw_samples.end());
}

auto RemoveCacheFile(const fs::path &cache_file_path, const char *description) -> astl_status_code {
  std::error_code error_code;
  fs::remove(cache_file_path, error_code);
  if (error_code) {
    ASTL_LOG_ERROR("RemoveCacheFile: failed to remove {} {}: {}", description, cache_file_path.string(),
                   error_code.message());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

auto WriteFilteredRawSampleBatch(std::ofstream &out_stream, const fs::path &temp_cache_file_path,
                                 const std::vector<RawSampledData> &raw_samples) -> astl_status_code {
  if (!out_stream.is_open()) {
    out_stream.open(temp_cache_file_path, std::ios::binary | std::ios::out);
    if (!out_stream) {
      ASTL_LOG_ERROR("WriteFilteredRawSampleBatch: failed to create filtered cache file {}",
                     temp_cache_file_path.string());
      return ASTL_STATUS_INTERNAL_ERROR;
    }
  }

  const auto write_status = ProtobufSerDes::Serialize(raw_samples, out_stream);
  if (write_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("WriteFilteredRawSampleBatch: failed to serialize filtered raw samples to {}: {}",
                   temp_cache_file_path.string(), astlStatusString(write_status));
  }
  return write_status;
}

auto WriteFilteredRawSampleBatches(std::istream &in_stream, const fs::path &temp_cache_file_path,
                                   const ClockCorrelationMap &correlations, std::span<const astl_crop_window_t> windows)
    -> std::expected<bool, astl_status_code> {
  std::ofstream                        out_stream;
  bool                                 wrote_filtered_samples = false;
  ProtobufSerDes::RawSampleBatchReader reader{in_stream};

  while (true) {
    auto raw_samples_or_error = reader.ReadNext();
    if (!raw_samples_or_error) {
      return std::unexpected(raw_samples_or_error.error());
    }
    if (raw_samples_or_error->empty()) {
      return wrote_filtered_samples;
    }

    auto raw_samples = std::move(*raw_samples_or_error);
    FilterRawSamples(raw_samples, correlations, windows);
    if (raw_samples.empty()) {
      continue;
    }

    const auto write_status = WriteFilteredRawSampleBatch(out_stream, temp_cache_file_path, raw_samples);
    if (write_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(write_status);
    }

    wrote_filtered_samples = true;
  }
}

auto ReplaceRawSampleCacheFile(const fs::path &cache_file_path, const fs::path &temp_cache_file_path)
    -> astl_status_code {
  const auto remove_status = RemoveCacheFile(cache_file_path, "raw sample cache file");
  if (remove_status != ASTL_STATUS_SUCCESS) {
    (void)RemoveCacheFile(temp_cache_file_path, "stale temp cache file");
    return remove_status;
  }

  std::error_code error_code;
  fs::rename(temp_cache_file_path, cache_file_path, error_code);
  if (error_code) {
    ASTL_LOG_ERROR("ReplaceRawSampleCacheFile: failed to replace raw sample cache file {}: {}",
                   cache_file_path.string(), error_code.message());
    (void)RemoveCacheFile(temp_cache_file_path, "stale temp cache file");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

}  // namespace

auto Orchestrator::EnsureProcessedSamplesLoadedForTarget(const ITarget *target) -> void {
  auto metric_handles_result = _metric_manager->GetAvailableMetrics(target);
  if (metric_handles_result) {
    for (const auto *const handle : *metric_handles_result) {
      auto metric_result = _metric_manager->GetMetricOnTarget(handle, target);
      if (metric_result) {
        (void)GetProcessedMetricSamples(*metric_result, target);
      }
    }
  }

  auto counter_handles_result = _metric_manager->GetAvailableCounters(target);
  if (counter_handles_result) {
    for (const auto *const handle : *counter_handles_result) {
      auto counter_result = _metric_manager->GetCounterOnTarget(handle, target);
      if (counter_result) {
        (void)GetProcessedMetricSamples(*counter_result, target);
      }
    }
  }
}

auto Orchestrator::FilterProcessedSamplesOnTarget(const ITarget *target, std::span<const astl_crop_window_t> windows)
    -> void {
  std::lock_guard lock{_processed_samples_mtx};
  auto            target_it = _processed_samples.find(target);
  if (target_it == _processed_samples.end()) {
    return;
  }

  for (auto &metric_samples : target_it->second) {
    FilterProcessedSamples(metric_samples.second, windows);
  }
}

auto Orchestrator::FilterRawSamplesOnTarget(const ITarget *target, const ClockCorrelationMap &correlations,
                                            std::span<const astl_crop_window_t> windows) -> void {
  std::lock_guard raw_lock{_raw_samples_mtx};
  auto            raw_it = _raw_samples.find(target);
  if (raw_it != _raw_samples.end() && !raw_it->second.empty()) {
    FilterRawSamples(raw_it->second, correlations, windows);
  }
}

auto Orchestrator::FilterRawSampleCacheFile(const ITarget *target, const ClockCorrelationMap &correlations,
                                            std::span<const astl_crop_window_t> windows) -> astl_status_code {
  const auto cache_file_path = _cache_dir / (GetStableTargetKey(*target) + kAstlFileExtension);
  if (!fs::exists(cache_file_path)) {
    return ASTL_STATUS_SUCCESS;
  }

  std::ifstream in_stream(cache_file_path, std::ios::binary);
  if (!in_stream) {
    ASTL_LOG_ERROR("FilterRawSampleCacheFile: failed to open raw sample cache file {}", cache_file_path.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto temp_cache_file_path = cache_file_path;
  temp_cache_file_path += ".tmp";

  const auto remove_status = RemoveCacheFile(temp_cache_file_path, "stale temp cache file");
  if (remove_status != ASTL_STATUS_SUCCESS) {
    return remove_status;
  }

  auto wrote_filtered_samples = WriteFilteredRawSampleBatches(in_stream, temp_cache_file_path, correlations, windows);
  in_stream.close();
  if (!wrote_filtered_samples) {
    ASTL_LOG_ERROR("FilterRawSampleCacheFile: failed to filter raw sample cache file {}: {}", cache_file_path.string(),
                   astlStatusString(wrote_filtered_samples.error()));
    (void)RemoveCacheFile(temp_cache_file_path, "stale temp cache file");
    return wrote_filtered_samples.error();
  }

  if (!*wrote_filtered_samples) {
    return RemoveCacheFile(cache_file_path, "raw sample cache file");
  }

  return ReplaceRawSampleCacheFile(cache_file_path, temp_cache_file_path);
}

auto Orchestrator::CropSamplesOnTarget(const ITarget *target, std::span<const astl_crop_window_t> windows)
    -> astl_status_code {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!_metric_manager) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto consolidated_windows = ConsolidateCropWindows(windows);
  windows                         = std::span<const astl_crop_window_t>{consolidated_windows};

  // Reject crop while collection is STARTED or PAUSED on this target.
  {
    auto state_result = GetTargetCollectionState(target);
    if (!state_result) {
      return state_result.error();
    }
    if (*state_result == TargetCollectionState::STARTED || *state_result == TargetCollectionState::PAUSED) {
      return ASTL_STATUS_COLLECTION_NOT_STOPPED;
    }
  }

  // Step 1: Ensure processed samples for all metrics on this target are populated in memory.
  // GetProcessedMetricSamples triggers lazy rebuild from disk when samples are not yet loaded.
  // Must be called without _processed_samples_mtx held.
  EnsureProcessedSamplesLoadedForTarget(target);

  // Step 2: Filter in-memory processed samples for all metrics on this target.
  FilterProcessedSamplesOnTarget(target, windows);

  // Obtain clock correlations once; used for both in-memory and on-disk raw sample filtering.
  const ClockCorrelationMap correlations = _metric_manager->GetClockCorrelations();

  // Step 3: Filter in-memory raw samples for this target (non-empty only before StopCollection is called,
  // but handled defensively here).
  FilterRawSamplesOnTarget(target, correlations, windows);

  // Step 4: Filter the on-disk raw sample cache file.
  const auto cache_filter_status = FilterRawSampleCacheFile(target, correlations, windows);
  if (cache_filter_status != ASTL_STATUS_SUCCESS) {
    return cache_filter_status;
  }

  // Step 5: Inject CROP_BEGIN / CROP_END lifecycle events for each window boundary.
  // Injected after filtering so the crop events themselves are never cropped out.
  // (see astl_lifecycle_event_type_t in astl_telemetry.h).
  // Ensure the lifecycle-event metric exists before recording any crop boundary events.
  EnsureLifecycleEventMetricForTarget(target);
  for (const auto &window : consolidated_windows) {
    if (window.start_ts != 0U) {
      const ProcessedSampleTimestamp begin_ts{
          std::chrono::duration<int64_t, std::nano>{static_cast<int64_t>(window.start_ts)}};
      const auto inject_status = _metric_manager->InjectLifecycleEvent(
          target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN), begin_ts);
      if (inject_status != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_WARNING("CropSamplesOnTarget: failed to inject crop-begin event for '{}' ({})", target->Name(),
                         astlStatusString(inject_status));
      }
    }
    if (window.end_ts != 0U) {
      const ProcessedSampleTimestamp end_ts{
          std::chrono::duration<int64_t, std::nano>{static_cast<int64_t>(window.end_ts)}};
      const auto inject_status =
          _metric_manager->InjectLifecycleEvent(target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_END), end_ts);
      if (inject_status != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_WARNING("CropSamplesOnTarget: failed to inject crop-end event for '{}' ({})", target->Name(),
                         astlStatusString(inject_status));
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::CropMetricSamplesOnTarget(const ITarget *target, const IMetric *metric,
                                             std::span<const astl_crop_window_t> windows) -> astl_status_code {
  if (!target || !metric) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!_metric_manager) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto consolidated_windows = ConsolidateCropWindows(windows);
  windows                         = std::span<const astl_crop_window_t>{consolidated_windows};

  // Reject crop while collection is STARTED or PAUSED on this target.
  {
    auto state_result = GetTargetCollectionState(target);
    if (!state_result) {
      return state_result.error();
    }
    if (*state_result == TargetCollectionState::STARTED || *state_result == TargetCollectionState::PAUSED) {
      return ASTL_STATUS_COLLECTION_NOT_STOPPED;
    }
  }

  // Step 1: Ensure in-memory processed samples are populated for this (target, metric) pair.
  (void)GetProcessedMetricSamples(metric, target);

  // Step 2: Filter in-memory processed samples for this (target, metric) pair only.
  {
    std::lock_guard lock{_processed_samples_mtx};
    auto            target_it = _processed_samples.find(target);
    if (target_it != _processed_samples.end()) {
      auto metric_it = target_it->second.find(metric);
      if (metric_it != target_it->second.end()) {
        FilterProcessedSamples(metric_it->second, windows);
      }
    }
  }

  // Note: the on-disk raw sample cache is shared across all metrics for a target.
  // Filtering it here would discard raw samples that belong to other metrics.
  // Callers that require the raw cache to be updated for persistence should use
  // CropSamplesOnTarget instead.
  ASTL_LOG_WARNING(
      "CropMetricSamplesOnTarget: on-disk raw sample cache was NOT filtered for metric '{}' on target "
      "'{}'. A future rebuild from cache will regenerate unfiltered samples. Use CropSamplesOnTarget to "
      "persist the crop.",
      metric->Name(), target->Name());

  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
