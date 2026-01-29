#include "orchestrator/orchestrator.hpp"

#include <algorithm>  // for std::max used in bulk reserve heuristic
#include <filesystem>
#include <fstream>
#include <functional>  // for std::reference_wrapper in expected return types

#include "astl/astl_errors.h"
#include "astl_defines.hpp"
#include "astl_logger.hpp"
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
}

Orchestrator::~Orchestrator() {
  // Best-effort cleanup; ignore status in destructor.
  std::error_code err_code;
  std::filesystem::remove_all(_cache_dir, err_code);
  if (err_code) {
    ASTL_LOG_WARNING("Orchestrator destructor failed to remove cache dir '{}': {}", _cache_dir.string(),
                     err_code.message());
  }
  (void)_collector_manager->UnregisterRawSampleSink(this);
  (void)_metric_manager->UnregisterProcessedSampleSink(this);
}

auto Orchestrator::InitializeInstance(std::unique_ptr<ITopologyManager>  topology_manager,
                                      std::unique_ptr<ICollectorManager> collector_manager,
                                      std::unique_ptr<IMetricManager>    metric_manager,
                                      std::unique_ptr<IOutputManager> output_manager, fs::path cache_dir_path) -> void {
  std::scoped_lock lock(GetMutex());
  if (!Orchestrator::instance_) {
    Orchestrator::instance_ =
        std::make_unique<Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                       std::move(metric_manager), std::move(output_manager), cache_dir_path);
  }
}

auto Orchestrator::GetInstance()
    -> std::expected<std::reference_wrapper<std::unique_ptr<Orchestrator>>, astl_status_code> {
  if (instance_) {
    return std::reference_wrapper<std::unique_ptr<Orchestrator>>(instance_);
  }

  // Lazy construction without taking the mutex (convention: only InitializeInstance guards creation)
  auto configuration = astl::ConfigurationManager::GetConfiguration();
  if (!configuration) {
    return std::unexpected(configuration.error());
  }
  auto astl_file_path = astl::GetEnvVar(astl::EnvVar::ASTL_LOAD_FILE_PATH);
  if (!astl_file_path.empty()) {
    configuration->astl_file_path = astl_file_path;
  }
  astl_status_code status = BuildOrchestrator(configuration.value());
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  if (!instance_) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return instance_;
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
  _metric_manager->RemoveAllMetrics();
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ConfigureCounterCollection(const ITarget                         *target,
                                              const astl_collection_parameters_t    *collection_params,
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
  auto available_counters = _metric_manager->GetAvailableCounters(target);
  if (!available_counters) {
    return available_counters.error();
  }
  for (const auto &counter : counters) {
    auto counter_index = std::ranges::find_if(
        available_counters.value(), [counter](auto const available_counter) { return available_counter == counter; });
    if (counter_index == std::end(available_counters.value())) {
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
  return status;
}

auto Orchestrator::ConfigureMetricCollection(const ITarget                        *target,
                                             const astl_collection_parameters_t   *collection_params,
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
  auto available_metrics = _metric_manager->GetAvailableMetrics(target);
  if (!available_metrics) {
    return available_metrics.error();
  }

  // check for supported metrics
  for (const auto &metric : metrics) {
    auto metric_index = std::find_if(std::begin(available_metrics.value()), std::end(available_metrics.value()),
                                     [metric](auto const &available_metric) { return available_metric == metric; });
    if (metric_index == std::end(available_metrics.value())) {
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
  return status;
}

auto Orchestrator::StartCollection(const ITarget *target) -> astl_status_code {
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

  std::unique_lock lock{_raw_samples_mtx};
  _raw_samples[target].clear();
  lock.unlock();  // in case _collector_manager runs operations on Start that try to sink samples to us
  return _collector_manager->StartOnTarget(target);
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
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

auto Orchestrator::ResumeCollection(const ITarget *target) -> astl_status_code {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return ASTL_STATUS_NOT_IMPLEMENTED;
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

  auto status = _collector_manager->StopOnTarget(target);  // finalize collector state
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  std::lock_guard lock{_raw_samples_mtx};

  // serialize any remaining in-memory samples for this target into the batch file, then clear
  auto iter = _raw_samples.find(target);
  if (iter != _raw_samples.end() && !iter->second.empty()) {
    auto res = astl::ProtobufSerDes::SerializeCurrentBatch(target->Name(), iter->second, _cache_dir);
    if (res != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to serialize remaining samples for {}", target->Name());
      return res;
    }
    iter->second.clear();
  }

  // if we've now stopped collection on all the targets, finalize metric processing and output
  if (!_collector_manager->IsAnyTargetBeingCollected()) {
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }

    // Emit Perfetto trace (if requested) after metrics are summarized (processed samples complete)
    EmitPerfettoTraceIfRequested();

    // Emit Interval CSV trace (if requested) after metrics are summarized (processed samples complete)
    EmitIntervalCsvIfRequested();

    // Emit Summary CSV (if requested) after metrics are summarized (processed samples complete)
    EmitSummaryCsvIfRequested();
  }

  auto save_astl_file_path = GetEnvVar(astl::EnvVar::ASTL_SAVE_FILE_PATH);
  if (!save_astl_file_path.empty()) {
    status = SaveToFile(save_astl_file_path);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Orchestrator::StopCollection failed to save state to astl file '{}': {}", save_astl_file_path,
                     astlStatusString(status));
      return status;
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::EmitPerfettoTraceIfRequested() -> void {
  if (_perfetto_emitted) {  // already emitted for this collection lifecycle
    return;
  }
  std::string perfetto_path = astl::GetEnvVar(astl::EnvVar::ASTL_OUTPUT_PERFETTO);
  if (perfetto_path.empty()) {
    return;  // no-op if unset
  }
  // Acquire processed samples snapshot under lock to avoid race with late sample insertion.
  std::lock_guard processed_lock{_processed_samples_mtx};
  if (_processed_samples.empty()) {
    ASTL_LOG_INFO("Perfetto trace requested but no processed samples available; emitting empty trace");
  }
  if (!_output_manager) {
    ASTL_LOG_ERROR("EmitPerfettoTraceIfRequested: OutputManager unavailable");
    return;
  }
  // Dispatch all processed samples via OutputManager using PERFETTO type.
  astl_status_code status =
      _output_manager->OutputProcessedSamples(_processed_samples, OutputType::PERFETTO, nullptr, nullptr);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("EmitPerfettoTraceIfRequested: failed to emit Perfetto trace (status={})", status);
    return;
  }
  _perfetto_emitted = true;
  ASTL_LOG_INFO("Perfetto trace emission completed (env path='{}')", perfetto_path);
}

auto Orchestrator::EmitIntervalCsvIfRequested() -> void {
  if (_intervalcsv_emitted) {
    return;
  }
  std::string csv_path = astl::GetEnvVar(astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV);
  if (csv_path.empty()) {
    return;  // nothing to do
  }
  std::lock_guard processed_lock{_processed_samples_mtx};
  if (!_output_manager) {
    ASTL_LOG_ERROR("EmitIntervalCsvIfRequested: OutputManager unavailable");
    return;
  }
  astl_status_code status =
      _output_manager->OutputProcessedSamples(_processed_samples, OutputType::INTERVAL_CSV, nullptr, nullptr);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("EmitIntervalCsvIfRequested: failed to emit interval CSV (status={})", status);
    return;
  }
  _intervalcsv_emitted = true;
  ASTL_LOG_INFO("Interval CSV emission completed (env path='{}')", csv_path);
}

auto Orchestrator::EmitSummaryCsvIfRequested() -> void {
  std::string csv_path = astl::GetEnvVar(astl::EnvVar::ASTL_OUTPUT_SUMMARY_CSV);
  if (csv_path.empty()) {
    return;  // no-op if unset
  }
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

auto Orchestrator::SinkRawSamples(const ITarget *target, std::span<RawSampledData> raw_samples) -> astl_status_code {
  if (!target) {
    ASTL_LOG_ERROR("Orchestrator::SinkSamples called with null target");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  /* Get the target name - just for logging */
  astl_target_properties_t properties;
  auto                     result = target->GetProperties(&properties);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }
  ASTL_LOG_DEBUG("Received {} samples for target {}", raw_samples.size(), properties._name);

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
      auto timestamp_ns = sample.timestamp.time_since_epoch().count();
      auto value        = sample.value;
      ASTL_LOG_DEBUG("Raw Sample - timestamp (ns since epoch): {}, value: {}", timestamp_ns, value);
    }

    if (!batch_samples.empty()) {
      auto res = astl::ProtobufSerDes::SerializeCurrentBatch(properties._name, batch_samples, _cache_dir);
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
  const fs::path file_path = _cache_dir / (target->Name() + kAstlFileExtension);

  if (!fs::exists(fs::path(_cache_dir))) {
    ASTL_LOG_DEBUG("No cache directory exists; skipping raw sample deserialization for target {}", target->Name());
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
    // ProcessRawSamples sinks results back via SinkProcessedSamples() - don't hold the mutex here.
    RawSamplesMap raw_samples{
        {target, std::move(*raw)}
    };

    if (auto status = _metric_manager->ProcessRawSamples(raw_samples); status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }

    if (auto status = _metric_manager->SummarizeMetrics(); status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
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

auto Orchestrator::SaveToFile(std::filesystem::path file_path) -> astl_status_code {
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

  mz::ZipDirectory(cache_dir_path, file_path);

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

  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
