#include "astl_impl.hpp"

#include <algorithm>  // for std::max used in bulk reserve heuristic

#include "astl/astl_errors.h"
#include "astl_logger.hpp"

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
}  // namespace

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
  _collector_manager->RegisterRawSampleSink(this);
  _metric_manager->RegisterProcessedSampleSink(this);
}

Orchestrator::~Orchestrator() {
  _collector_manager->UnregisterRawSampleSink(this);
  _metric_manager->UnregisterProcessedSampleSink(this);
}

void Orchestrator::InitializeInstance(std::unique_ptr<ITopologyManager>  topology_manager,
                                      std::unique_ptr<ICollectorManager> collector_manager,
                                      std::unique_ptr<IMetricManager>    metric_manager,
                                      std::unique_ptr<IOutputManager>    output_manager) {
  std::scoped_lock lock(GetMutex());
  auto            &inst = GetInstance();
  if (!inst) {
    inst = std::make_unique<Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                          std::move(metric_manager), std::move(output_manager));
  }
}

std::unique_ptr<Orchestrator> &Orchestrator::GetInstance() {
  static std::unique_ptr<Orchestrator> instance;
  return instance;
}

std::mutex &Orchestrator::GetMutex() {
  static std::mutex initialization_mutex;
  return initialization_mutex;
}

std::vector<std::unique_ptr<ITarget>> const &Orchestrator::GetTargets() const {
  return _topology_manager->GetTargets();
}

astl_status_code Orchestrator::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) {
  _topology_manager->SetTargets(std::move(new_targets));
  return ASTL_STATUS_SUCCESS;
}

astl_status_code Orchestrator::ConfigureCounterCollection(const ITarget                      *target,
                                                          const astl_collection_parameters_t *collection_params,
                                                          std::span<const ICounter *>         counters) {
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

astl_status_code Orchestrator::ConfigureMetricCollection(const ITarget                        *target,
                                                         const astl_collection_parameters_t   *collection_params,
                                                         std::span<const astl_metric_handle_t> metrics) {
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

astl_status_code Orchestrator::StartCollection(const ITarget *target) {
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

  _raw_samples[target].clear();
  return _collector_manager->StartOnTarget(target);
}

astl_status_code Orchestrator::ReadImmediate(const ITarget *target) {
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::ReadImmediate called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return _collector_manager->ReadImmediateOnTarget(target);
}

astl_status_code Orchestrator::PauseCollection(const ITarget *target) {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

astl_status_code Orchestrator::ResumeCollection(const ITarget *target) {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

astl_status_code Orchestrator::StopCollection(const ITarget *target) {
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

  astl_status_code status = _metric_manager->ProcessRawSamples(_raw_samples);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  status = _metric_manager->SummarizeMetrics();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  status = _collector_manager->StopOnTarget(target);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  return ASTL_STATUS_SUCCESS;
}

std::expected<uint32_t, astl_status_code> Orchestrator::GetCounterSampleCount(const ITarget  *target,
                                                                              const ICounter *counter) const {
  const std::vector<std::unique_ptr<ITarget>> &targets = _topology_manager->GetTargets();
  auto                                         index   = std::find_if(std::begin(targets), std::end(targets),
                                                                      [target](auto const &owned_target) { return owned_target.get() == target; });

  if (index == std::end(targets)) {
    return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
  (void)counter;  // unused since unimplemented for now
  return std::unexpected(ASTL_STATUS_INVALID_COUNTER_HANDLE);
}

astl_status_code Orchestrator::SinkRawSamples(const ITarget *target, std::span<RawSampledData> raw_samples) {
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
    auto &target_samples_vec = _raw_samples[target];

    // Bulk reserve once based on total required size rather than per-sample growth decisions.
    // We keep the 1.5x + bias heuristic but ensure we always meet the exact required size.
    const auto required_size = target_samples_vec.size() + raw_samples.size();
    if (target_samples_vec.capacity() < required_size) {
      auto current = target_samples_vec.size();
      auto heuristic_cap =
          static_cast<size_t>(current * kRawSampleGrowthNumerator / kRawSampleGrowthDenominator + kRawSampleGrowthBias);
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
  }

  return ASTL_STATUS_SUCCESS;
}

astl_status_code Orchestrator::SinkProcessedSamples(const ITarget *target, const IMetric *metric,
                                                    std::span<const ProcessedSampledData> processed_samples) {
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
    auto &metric_map = _processed_samples[target];
    auto &vec        = metric_map[metric];
    // Batch reserve up-front to avoid repeated reallocations when adding N new processed samples.
    if (vec.capacity() < vec.size() + processed_samples.size()) {
      auto required = vec.size() + processed_samples.size();
      // Same growth heuristic (1.5x + bias) abstracted via named constants.
      auto new_cap = static_cast<size_t>(required * kProcSampleGrowthNumerator / kProcSampleGrowthDenominator +
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
  auto target_it = _processed_samples.find(target);
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
