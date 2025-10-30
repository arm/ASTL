#include "orchestrator/orchestrator.hpp"

#include <algorithm>  // for std::max used in bulk reserve heuristic
#include <cstdlib>    // for std::getenv
#include <filesystem>
#include <fstream>

#include "astl/astl_errors.h"
#include "astl_defines.hpp"
#include "astl_logger.hpp"
#include "serdes/protobuf_serdes.hpp"

namespace astl {

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
                           std::unique_ptr<IOutputManager>    output_manager)
    : _topology_manager{std::move(topology_manager)},
      _collector_manager{std::move(collector_manager)},
      _metric_manager{std::move(metric_manager)},
      _output_manager{std::move(output_manager)} {
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
  (void)_collector_manager->UnregisterRawSampleSink(this);
  (void)_metric_manager->UnregisterProcessedSampleSink(this);
}

auto Orchestrator::InitializeInstance(std::unique_ptr<ITopologyManager>  topology_manager,
                                      std::unique_ptr<ICollectorManager> collector_manager,
                                      std::unique_ptr<IMetricManager>    metric_manager,
                                      std::unique_ptr<IOutputManager>    output_manager) -> void {
  std::scoped_lock lock(GetMutex());
  auto            &inst = GetInstance();
  if (!inst) {
    inst = std::make_unique<Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                          std::move(metric_manager), std::move(output_manager));
  }
}

auto Orchestrator::GetInstance() -> std::unique_ptr<Orchestrator> & {
  static std::unique_ptr<Orchestrator> instance;
  return instance;
}

auto Orchestrator::GetMutex() -> std::mutex & {
  static std::mutex initialization_mutex;
  return initialization_mutex;
}

auto Orchestrator::GetTargets() const -> std::vector<std::unique_ptr<ITarget>> const & {
  return _topology_manager->GetTargets();
}

auto Orchestrator::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code {
  _topology_manager->SetTargets(std::move(new_targets));
  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::ConfigureCounterCollection(const ITarget                      *target,
                                              const astl_collection_parameters_t *collection_params,
                                              std::span<const ICounter *>         counters) -> astl_status_code {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  // unused, since unimplemented
  (void)collection_params;
  (void)counters;
  return ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET;
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
    ASTL_LOG_TRACE("ConfigureMetricCollection for metric_handle {} on target {}", metric, target->Name());
    auto metric_index = std::find_if(std::begin(available_metrics.value()), std::end(available_metrics.value()),
                                     [metric](auto const &available_metric) { return available_metric == metric; });
    if (metric_index == std::end(available_metrics.value())) {
      ASTL_LOG_ERROR("Metric {} is not supported on target {}", metric, target->Name());
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

  std::unique_lock lock{_raw_samples_mtx};

  // serialize any remaining in-memory samples for this target into the batch file, then clear
  auto it = _raw_samples.find(target);
  if (it != _raw_samples.end() && !it->second.empty()) {
    auto res = astl::ProtobufSerDes::SerializeCurrentBatch(target->Name(), it->second);
    if (res != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to serialize remaining samples for {}", target->Name());
      return res;
    }
    it->second.clear();
  }

  // rebuild raw samples from serialized temporary file
  std::vector<RawSampledData> rebuilt_samples;
  const fs::path              dir       = "tmp";
  const auto                  file_path = dir / (target->Name() + ".astl");
  std::ifstream               cache_file(file_path, std::ios::binary);

  if (fs::exists(file_path)) {
    if (!cache_file) {
      ASTL_LOG_ERROR("Failed to open {} for reading", file_path.string());
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    auto deser = astl::ProtobufSerDes::Deserialize(cache_file);
    if (!deser.has_value()) {
      ASTL_LOG_ERROR("Failed to deserialize samples from {}: {}", file_path.string(), astlStatusString(deser.error()));
      return deser.error();
    }

    rebuilt_samples.insert(rebuilt_samples.end(), std::make_move_iterator(deser->begin()),
                           std::make_move_iterator(deser->end()));
  } else {
    ASTL_LOG_DEBUG("No temporary sample file for target {}", target->Name());
  }

  // TODO (ASTL-224): Handle batch raw sample processing instead of loading all into memory at once.
  RawSamplesMap raw_samples{
      {target, std::move(rebuilt_samples)}
  };

  // TODO(ASTL-209): Move CollectorManager::StopOnTarget before ProcessRawSamples when
  // implementing Orchestrator State Machine to ensure collection is stopped before processing begins.
  astl_status_code status = _metric_manager->ProcessRawSamples(raw_samples);

  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  status = _metric_manager->SummarizeMetrics();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Emit Perfetto trace (if requested) after metrics are summarized (processed samples complete)
  EmitPerfettoTraceIfRequested();

  // Emit Interval CSV trace (if requested) after metrics are summarized (processed samples complete)
  EmitIntervalCsvIfRequested();

  // Emit Summary CSV (if requested) after metrics are summarized (processed samples complete)
  EmitSummaryCsvIfRequested();

  lock.unlock();                                      // allow collector periodic samples to proceed while stopping
  status = _collector_manager->StopOnTarget(target);  // finalize collector state
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  return ASTL_STATUS_SUCCESS;
}

auto Orchestrator::EmitPerfettoTraceIfRequested() -> void {
  if (_perfetto_emitted) {  // already emitted for this collection lifecycle
    return;
  }
  std::string perfetto_path = astl::GetEnvVar("ASTL_OUTPUT_PERFETTO");
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
  std::string csv_path = astl::GetEnvVar("ASTL_OUTPUT_INTERVAL_CSV");
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
  std::string csv_path = astl::GetEnvVar("ASTL_OUTPUT_SUMMARY_CSV");
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

auto Orchestrator::GetCounterSampleCount(const ITarget *target, const ICounter *counter) const
    -> std::expected<uint32_t, astl_status_code> {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });

  if (index == std::end(targets)) {
    return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
  (void)counter;  // unused since unimplemented for now
  return std::unexpected(ASTL_STATUS_INVALID_COUNTER_HANDLE);
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
      ASTL_LOG_DEBUG("Sample - timestamp (ns since epoch): {}, value: {}", timestamp_ns, value);
    }

    if (!batch_samples.empty()) {
      auto res = astl::ProtobufSerDes::SerializeCurrentBatch(properties._name, batch_samples);
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

  /* Get the target name - just for logging */
  astl_target_properties_t target_properties;
  auto                     result = target->GetProperties(&target_properties);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }

  /* Get the target name - just for logging */
  astl_metric_properties_t metric_properties;
  result = metric->GetProperties(&metric_properties);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }

  ASTL_LOG_DEBUG("Received {} samples for metric {} on target {}", processed_samples.size(), metric_properties._name,
                 target_properties._name);

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
      ASTL_LOG_DEBUG("Sample - timestamp (ns since epoch): {}, value: {}", timestamp_ns, value);
    }
  }

  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Retrieve the collected samples for the given target and metric,
 *        or an error if the target+metric combination isn't valid
 */
auto Orchestrator::GetProcessedMetricSamples(const IMetric *metric, const ITarget *target) const
    -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code> {
  // Use find() to avoid modifying the map in this const method (operator[] would insert elements)
  std::scoped_lock lock{_processed_samples_mtx};
  auto             target_it = _processed_samples.find(target);
  if (target_it == _processed_samples.end()) {
    ASTL_LOG_ERROR("GetProcessedMetricSamples: No samples found for metric {} on target {}", metric->Name(),
                   target->Name());
    return std::unexpected{ASTL_STATUS_NO_DATA_COLLECTED};
  }

  const auto &metric_map = target_it->second;
  auto        metric_it  = metric_map.find(metric);
  if (metric_it == metric_map.end()) {
    ASTL_LOG_ERROR("GetProcessedMetricSamples: No samples found for metric {} on target {}", metric->Name(),
                   target->Name());
    return std::unexpected{ASTL_STATUS_NO_DATA_COLLECTED};
  }

  auto samples = std::span<const astl::ProcessedSampledData>(metric_it->second);
  return samples;  // implicit conversion to expected via value ctor
}
}  // namespace astl
