// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <expected>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "astl/astl.h"
#include "common/astl_defines.hpp"
#include "common/metric_config.hpp"
#include "common/string_pool.hpp"
#include "common/system_info.hpp"
#include "config/configuration_manager.hpp"
#include "metric/counter.hpp"
#include "metric/finite_set_metric.hpp"
#include "metric/i_metric.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/residency_metric.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/i_output_manager.hpp"
#include "output/summarizer.hpp"
#include "target.hpp"

/***********************************************************************************
 **********************               HELPERS               ************************
 **********************************************************************************/

/** @brief Confirms that the given non-null target_handle matches some known ITarget */
auto GetTargetFromHandle(astl_target_handle_t target_handle) noexcept
    -> std::expected<const astl::ITarget*, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator      = orchestrator_or_error->get();
  auto const& available_targets = orchestrator->GetTargets();
  const auto* target            = static_cast<const astl::ITarget*>(target_handle);
  auto        is_handle_match   = [target](auto& target_iter) noexcept -> bool { return target_iter.get() == target; };

  using std::begin, std::end;
  if (std::any_of(begin(available_targets), end(available_targets), is_handle_match)) {
    return target;
  }
  return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
}

auto GetMetricManager() noexcept -> std::expected<astl::IMetricManager*, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator   = orchestrator_or_error->get();
  const auto& metric_manager = orchestrator->GetMetricManager();
  if (!metric_manager) {
    ASTL_LOG_ERROR("No metric manager assigned to orchestrator");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return metric_manager.get();
}

auto SwitchSystemInfoToHostCapture() noexcept -> void { astl::ClearLoadedPlatformInfo(); }

auto GetCApiMutex() noexcept -> std::mutex& {
  // Global C-API serialization point.
  // Purpose: make exported C entrypoints safe under concurrent calls while preserving
  // existing non-thread-safe internals and singleton lazy-init flows.
  // We intentionally use a non-recursive mutex and avoid lock re-entry patterns.
  static std::mutex c_api_mutex;
  return c_api_mutex;
}

auto GetConfiguredTargets(astl::Orchestrator& orchestrator) noexcept -> std::vector<const astl::ITarget*> {
  std::vector<const astl::ITarget*> configured_targets;
  const auto                        target_states = orchestrator.GetAllTargetCollectionStates();
  for (const auto& target : orchestrator.GetTargets()) {
    auto state_it = target_states.find(target.get());
    if (state_it != target_states.end() && state_it->second == astl::Orchestrator::TargetCollectionState::CONFIGURED) {
      configured_targets.push_back(target.get());
    }
  }
  return configured_targets;
}

auto StartConfiguredTargets(astl::Orchestrator& orchestrator, bool start_paused) noexcept -> astl_status_code {
  const auto configured_targets = GetConfiguredTargets(orchestrator);
  if (configured_targets.empty()) {
    return ASTL_STATUS_COLLECTION_NOT_CONFIGURED;
  }

  std::vector<const astl::ITarget*> started_targets;
  started_targets.reserve(configured_targets.size());
  for (const auto* target : configured_targets) {
    const auto status =
        start_paused ? orchestrator.StartCollectionPaused(target) : orchestrator.StartCollection(target);
    if (status != ASTL_STATUS_SUCCESS) {
      astl_status_code rollback_status = ASTL_STATUS_SUCCESS;
      for (auto it = started_targets.rbegin(); it != started_targets.rend(); ++it) {
        const auto stop_status = orchestrator.RollbackStartedCollectionToConfigured(*it);
        if (stop_status != ASTL_STATUS_SUCCESS && rollback_status == ASTL_STATUS_SUCCESS) {
          rollback_status = stop_status;
        }
      }
      if (rollback_status != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to roll back prior target starts after aggregate start failure: {}",
                       astlStatusString(rollback_status));
        return rollback_status;
      }
      return status;
    }
    started_targets.push_back(target);
  }
  return ASTL_STATUS_SUCCESS;
}

auto GetCounterFromHandle(astl_counter_handle_t counter_handle, const astl::ITarget* target) noexcept
    -> std::expected<const astl::IMetric*, astl_status_code> {
  auto const& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return std::unexpected(get_metric_manager_result.error());
  }
  auto* metric_manager   = *get_metric_manager_result;
  auto  counter_or_error = metric_manager->GetCounterOnTarget(counter_handle, target);
  if (!counter_or_error.has_value()) {
    return std::unexpected{counter_or_error.error()};
  }
  const auto* counter = *counter_or_error;
  return counter;
}

auto GetMetricFromHandle(astl_metric_handle_t metric_handle, astl_target_handle_t target_handle) noexcept
    -> std::expected<const astl::IMetric*, astl_status_code> {
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return std::unexpected(get_metric_manager_result.error());
  }
  auto* metric_manager = *get_metric_manager_result;

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return std::unexpected(get_target_result.error());
  }
  const astl::ITarget* target = *get_target_result;

  auto metric_or_error = metric_manager->GetMetricOnTarget(metric_handle, target);
  if (!metric_or_error.has_value()) {
    return std::unexpected{metric_or_error.error()};
  }
  const astl::IMetric* metric = *metric_or_error;

  return metric;
}

auto GetProcessedMetricSamples(const astl::IMetric* metric, const astl::ITarget* target) noexcept
    -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator = orchestrator_or_error->get();

  auto samples_result = orchestrator->GetProcessedMetricSamples(metric, target);

  return samples_result;
}

auto ValidateTimestampRange(const char* api_name, uint64_t start_ts, uint64_t end_ts) noexcept -> astl_status_code {
  if (start_ts != 0 && end_ts != 0 && start_ts > end_ts) {
    ASTL_LOG_ERROR("{}: start_ts ({}) must be <= end_ts ({})", api_name, start_ts, end_ts);
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  return ASTL_STATUS_SUCCESS;
}

template <typename ParamsT>
auto ValidateTimestampedApiParams(const ParamsT* params, const char* api_name) noexcept -> astl_status_code {
  auto status = ValidateApiParams(params);
  if (status == ASTL_STATUS_SUCCESS) {
    status = ValidateTimestampRange(api_name, params->start_ts, params->end_ts);
  }
  return status;
}

auto SampleIsWithinTimestampRange(const astl::ProcessedSampledData& sample, uint64_t start_ts, uint64_t end_ts) noexcept
    -> bool {
  const uint64_t timestamp = static_cast<uint64_t>(sample.timestamp.time_since_epoch().count());
  if (start_ts != 0 && timestamp < start_ts) {
    return false;
  }
  if (end_ts != 0 && timestamp > end_ts) {
    return false;
  }
  return true;
}

auto CountSamplesInTimestampRange(std::span<const astl::ProcessedSampledData> samples, uint64_t start_ts,
                                  uint64_t end_ts) -> size_t {
  return static_cast<size_t>(
      std::count_if(samples.begin(), samples.end(), [start_ts, end_ts](const astl::ProcessedSampledData& sample) {
        return SampleIsWithinTimestampRange(sample, start_ts, end_ts);
      }));
}

auto FilterSamplesInTimestampRange(std::span<const astl::ProcessedSampledData> samples, uint64_t start_ts,
                                   uint64_t end_ts) -> std::vector<astl::ProcessedSampledData> {
  std::vector<astl::ProcessedSampledData> filtered_samples;
  filtered_samples.reserve(CountSamplesInTimestampRange(samples, start_ts, end_ts));
  std::copy_if(samples.begin(), samples.end(), std::back_inserter(filtered_samples),
               [start_ts, end_ts](const astl::ProcessedSampledData& sample) {
                 return SampleIsWithinTimestampRange(sample, start_ts, end_ts);
               });
  return filtered_samples;
}

auto ConvertProcessedSampleToAstlSample(const astl::ProcessedSampledData& processed_sample) -> astl_sample_t {
  const auto union_value = processed_sample.value.ToAstlUnionValue().first;
  return astl_sample_t{.timestamp = static_cast<uint64_t>(processed_sample.timestamp.time_since_epoch().count()),
                       .value     = union_value};
}

auto WriteSamplesInTimestampRange(std::span<const astl::ProcessedSampledData> processed_samples,
                                  std::span<astl_sample_t> output_samples, uint32_t* output_count, uint64_t start_ts,
                                  uint64_t end_ts) noexcept -> astl_status_code {
  uint32_t written = 0;
  for (const auto& processed_sample : processed_samples) {
    if (!SampleIsWithinTimestampRange(processed_sample, start_ts, end_ts)) {
      continue;
    }
    if (written >= output_samples.size()) {
      break;
    }
    output_samples[written] = ConvertProcessedSampleToAstlSample(processed_sample);
    ++written;
  }
  *output_count = written;
  return ASTL_STATUS_SUCCESS;
}

constexpr uint32_t kFirstElementIdx{0};

auto GetStructVersionStatus(size_t given_size, size_t expected_size) noexcept -> astl_status_code {
  if (given_size < expected_size) {
    return ASTL_STATUS_OLD_STRUCT_VERSION;
  }
  if (given_size > expected_size) {
    return ASTL_STATUS_NEW_STRUCT_VERSION;
  }
  return ASTL_STATUS_SUCCESS;
}

template <typename StructT>
auto GetStructVersionStatus(const StructT& given_struct) noexcept -> astl_status_code {
  return GetStructVersionStatus(given_struct.size, sizeof(StructT));
}

template <typename ParamsT>
auto ValidateApiParams(const ParamsT* params) noexcept -> astl_status_code {
  if (params == nullptr) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto struct_version_status = GetStructVersionStatus(*params);
  if (struct_version_status != ASTL_STATUS_SUCCESS) {
    return struct_version_status;
  }
  if (params->flags != 0U) {
    return ASTL_STATUS_INVALID_FLAG_VALUE;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ValidateCollectionParamsFlags(const astl_collection_params_t* collection_params) noexcept -> astl_status_code {
  const uint32_t k_allowed_collection_flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD |
                                              ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY |
                                              ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_INTERFERENCE;
  const uint32_t request_flags = collection_params->flags;
  if ((request_flags & ~k_allowed_collection_flags) != 0U) {
    return ASTL_STATUS_INVALID_FLAG_VALUE;
  }
  // Optimization flags are mutually exclusive.
  if (request_flags != 0U && ((request_flags & (request_flags - 1U)) != 0U)) {
    return ASTL_STATUS_INVALID_FLAG_VALUE;
  }
  return ASTL_STATUS_SUCCESS;
}

struct ResolvedTargetAndMetricManager {
  astl::IMetricManager* metric_manager{nullptr};
  const astl::ITarget*  target{nullptr};
};

template <typename Container>
auto GetFirstElementSizeField(Container const& elements) noexcept
    -> std::expected<decltype(elements[kFirstElementIdx].size), astl_status_code>;

auto ResolveTargetAndMetricManager(astl_target_handle_t target_handle) noexcept
    -> std::expected<ResolvedTargetAndMetricManager, astl_status_code> {
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return std::unexpected(get_metric_manager_result.error());
  }

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return std::unexpected(get_target_result.error());
  }

  return ResolvedTargetAndMetricManager{
      .metric_manager = *get_metric_manager_result,
      .target         = *get_target_result,
  };
}

template <typename HandleContainerT>
auto ValidateAvailableHandleCount(const HandleContainerT& available_handles, uint32_t output_buffer_size,
                                  uint32_t* out_count, astl_status_code empty_status) noexcept -> astl_status_code {
  if (available_handles.size() > output_buffer_size) {
    if (available_handles.size() <= std::numeric_limits<uint32_t>::max()) {
      *out_count = static_cast<uint32_t>(available_handles.size());
    }
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }
  if (available_handles.empty()) {
    *out_count = 0;
    return empty_status;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ValidateCounterOutputStruct(std::span<astl_counter_props_t> counter_props) noexcept -> astl_status_code {
  auto counter_struct_size = GetFirstElementSizeField(counter_props);
  if (!counter_struct_size) {
    return counter_struct_size.error();
  }
  return GetStructVersionStatus(counter_props.front());
}

template <typename CounterHandleContainerT>
auto PopulateCounterPropertiesForHandles(astl::IMetricManager*           metric_manager,
                                         const CounterHandleContainerT&  available_counters,
                                         std::span<astl_counter_props_t> output_counters,
                                         uint32_t* out_counter_count) noexcept -> astl_status_code {
  for (size_t index = 0; index < available_counters.size(); ++index) {
    const auto status = metric_manager->GetCounterProperties(available_counters[index], &output_counters[index]);
    if (status != ASTL_STATUS_SUCCESS) {
      *out_counter_count = static_cast<uint32_t>(index);
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto ValidateMetricOutputStruct(std::span<astl_metric_props_t> metric_props) noexcept -> astl_status_code {
  auto metric_struct_size = GetFirstElementSizeField(metric_props);
  if (!metric_struct_size) {
    return metric_struct_size.error();
  }
  return GetStructVersionStatus(metric_props.front());
}

template <typename MetricHandleContainerT>
auto PopulateMetricPropertiesForHandles(astl::IMetricManager*          metric_manager,
                                        const MetricHandleContainerT&  metric_handles,
                                        std::span<astl_metric_props_t> output_metrics,
                                        uint32_t*                      out_metric_count) noexcept -> astl_status_code {
  for (size_t index = 0; index < metric_handles.size(); ++index) {
    const auto status = metric_manager->GetProperties(metric_handles[index], &output_metrics[index]);
    if (status != ASTL_STATUS_SUCCESS) {
      *out_metric_count = static_cast<uint32_t>(index);
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

// Used to get the '_size' field of the first element in the span, array, etc of astl_target_props_t or other
// structs
template <typename Container>
auto GetFirstElementSizeField(Container const& elements) noexcept
    -> std::expected<decltype(elements[kFirstElementIdx].size), astl_status_code> {
  if (std::size(elements) == 0) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return elements[kFirstElementIdx].size;
}

auto PopulateMetricGroupProperties(astl::IMetricManager* metric_manager, astl_metric_group_handle_t group,
                                   astl_metric_group_props_t& properties) noexcept -> astl_status_code {
  return metric_manager->GetMetricGroupProperties(group, &properties);
}

auto PopulateMetricPropertiesForHandles(astl::IMetricManager*                 metric_manager,
                                        std::span<const astl_metric_handle_t> metric_handles,
                                        std::span<astl_metric_props_t> metric_buffer) noexcept -> astl_status_code {
  if (metric_handles.empty()) {
    return ASTL_STATUS_SUCCESS;
  }
  const auto metric_struct_status = GetStructVersionStatus(metric_buffer[0]);
  if (metric_struct_status != ASTL_STATUS_SUCCESS) {
    return metric_struct_status;
  }

  for (size_t metric_idx = 0; metric_idx < metric_handles.size(); ++metric_idx) {
    auto result = metric_manager->GetProperties(metric_handles[metric_idx], &metric_buffer[metric_idx]);
    if (result != ASTL_STATUS_SUCCESS) {
      return result;
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto GetOrchestratorInstance() -> std::expected<astl::Orchestrator*, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected(orchestrator_or_error.error());
  }
  return orchestrator_or_error->get().get();
}

struct MetricGroupOutputRequest {
  std::span<astl_metric_group_props_t> output;
  uint32_t*                            out_count{nullptr};
};

auto ParseMetricGroupOutputRequest(astl_metric_group_props_t* metric_groups, uint32_t* metric_group_count)
    -> std::expected<MetricGroupOutputRequest, astl_status_code> {
  if (!metric_groups || !metric_group_count || *metric_group_count == 0) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto output                           = std::span<astl_metric_group_props_t>{metric_groups, *metric_group_count};
  *metric_group_count                   = 0;
  const auto metric_group_struct_status = GetStructVersionStatus(output.front());
  if (metric_group_struct_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(metric_group_struct_status);
  }

  return MetricGroupOutputRequest{
      .output    = output,
      .out_count = metric_group_count,
  };
}

template <typename GroupContainerT>
auto PopulateMetricGroupPropertiesForHandles(astl::IMetricManager* metric_manager, const GroupContainerT& groups,
                                             std::span<astl_metric_group_props_t> output, uint32_t* out_count)
    -> astl_status_code {
  if (groups.size() > output.size()) {
    if (groups.size() <= std::numeric_limits<uint32_t>::max()) {
      *out_count = static_cast<uint32_t>(groups.size());
    }
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  for (size_t index = 0; index < groups.size(); ++index) {
    const auto status = PopulateMetricGroupProperties(metric_manager, groups[index], output[index]);
    if (status != ASTL_STATUS_SUCCESS) {
      *out_count = static_cast<uint32_t>(index);
      return status;
    }
  }

  *out_count = static_cast<uint32_t>(groups.size());
  return output.size() > groups.size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED : ASTL_STATUS_SUCCESS;
}

struct MetricPropertiesOutputRequest {
  std::span<astl_metric_props_t> output;
  uint32_t*                      out_count{nullptr};
};

auto ParseMetricPropertiesOutputRequest(astl_metric_props_t* metrics, uint32_t* metric_count)
    -> std::expected<MetricPropertiesOutputRequest, astl_status_code> {
  if (!metrics || !metric_count || *metric_count == 0) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  return MetricPropertiesOutputRequest{
      .output    = std::span<astl_metric_props_t>{metrics, *metric_count},
      .out_count = metric_count,
  };
}

struct MetricsOnTargetRequest {
  astl::IMetricManager*          metric_manager{nullptr};
  const astl::ITarget*           target{nullptr};
  std::span<astl_metric_props_t> output_metrics;
  uint32_t                       output_metric_capacity{0};
  uint32_t*                      out_metric_count{nullptr};
};

auto ParseMetricsOnTargetRequest(const astl_get_metrics_params_t& params)
    -> std::expected<MetricsOnTargetRequest, astl_status_code> {
  if (!params.target_handle || !params.metrics || !params.metric_count || *params.metric_count == 0) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  const auto output_metric_capacity = *params.metric_count;
  auto       output_metrics         = std::span<astl_metric_props_t>{params.metrics, output_metric_capacity};
  *params.metric_count              = 0;

  auto resolved_components = ResolveTargetAndMetricManager(params.target_handle);
  if (!resolved_components) {
    return std::unexpected(resolved_components.error());
  }

  return MetricsOnTargetRequest{
      .metric_manager         = resolved_components->metric_manager,
      .target                 = resolved_components->target,
      .output_metrics         = output_metrics,
      .output_metric_capacity = output_metric_capacity,
      .out_metric_count       = params.metric_count,
  };
}

template <typename MetricHandleContainerT>
auto PopulateMetricsOnTargetOutput(const MetricsOnTargetRequest& request,
                                   const MetricHandleContainerT& available_metrics) -> astl_status_code {
  auto status = ValidateAvailableHandleCount(available_metrics, request.output_metric_capacity,
                                             request.out_metric_count, ASTL_STATUS_NO_METRICS_FOUND);
  if (status == ASTL_STATUS_SUCCESS) {
    status = ValidateMetricOutputStruct(request.output_metrics);
  }
  if (status == ASTL_STATUS_SUCCESS) {
    status = PopulateMetricPropertiesForHandles(request.metric_manager, available_metrics, request.output_metrics,
                                                request.out_metric_count);
  }
  if (status == ASTL_STATUS_SUCCESS) {
    *request.out_metric_count = static_cast<uint32_t>(available_metrics.size());
    status = request.output_metrics.size() > available_metrics.size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
                                                                      : ASTL_STATUS_SUCCESS;
  }
  return status;
}

auto PopulateMetricsOnTargetResponse(const MetricsOnTargetRequest& request) -> astl_status_code {
  const auto available_metrics_result = request.metric_manager->GetAvailableMetrics(request.target);
  if (!available_metrics_result) {
    return available_metrics_result.error();
  }

  const auto& available_metrics = *available_metrics_result;
  if (available_metrics.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("metric_manager->GetMetrics reports absurdly large number of metrics: {}", available_metrics.size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return PopulateMetricsOnTargetOutput(request, available_metrics);
}

auto PopulateMetricPropertiesForMetricGroup(astl::IMetricManager*                 metric_manager,
                                            std::span<const astl_metric_handle_t> metrics_in_group,
                                            std::span<astl_metric_props_t> output_metrics, uint32_t* out_metric_count)
    -> astl_status_code {
  if (metrics_in_group.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("GetMetricsInGroup reports absurdly large number of metrics: {}", metrics_in_group.size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  const auto required_metric_count = static_cast<uint32_t>(metrics_in_group.size());
  if (output_metrics.size() < metrics_in_group.size()) {
    *out_metric_count = required_metric_count;
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  auto status = PopulateMetricPropertiesForHandles(metric_manager, metrics_in_group,
                                                   output_metrics.first(metrics_in_group.size()));
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  *out_metric_count = required_metric_count;
  return output_metrics.size() > metrics_in_group.size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED : ASTL_STATUS_SUCCESS;
}

struct SampleCountRequest {
  const astl::ITarget* target{nullptr};
  const astl::IMetric* metric{nullptr};
  uint32_t*            out_sample_count{nullptr};
};

auto ParseCounterSampleCountRequest(const astl_get_counter_sample_count_on_target_params_t& params)
    -> std::expected<SampleCountRequest, astl_status_code> {
  const bool missing_required_ptr =
      (params.target_handle == nullptr) || (params.counter_handle == nullptr) || (params.sample_count == nullptr);
  if (missing_required_ptr) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto target_or_error = GetTargetFromHandle(params.target_handle);
  if (!target_or_error) {
    return std::unexpected(target_or_error.error());
  }

  auto counter_or_error = GetCounterFromHandle(params.counter_handle, *target_or_error);
  if (!counter_or_error) {
    return std::unexpected(counter_or_error.error());
  }

  return SampleCountRequest{
      .target           = *target_or_error,
      .metric           = *counter_or_error,
      .out_sample_count = params.sample_count,
  };
}

auto ParseMetricSampleCountRequest(const astl_get_metric_sample_count_on_target_params_t& params)
    -> std::expected<SampleCountRequest, astl_status_code> {
  const bool missing_required_ptr =
      (params.target_handle == nullptr) || (params.metric_handle == nullptr) || (params.sample_count == nullptr);
  if (missing_required_ptr) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto target_or_error = GetTargetFromHandle(params.target_handle);
  if (!target_or_error) {
    return std::unexpected(target_or_error.error());
  }

  auto metric_or_error = GetMetricFromHandle(params.metric_handle, params.target_handle);
  if (!metric_or_error.has_value()) {
    ASTL_LOG_ERROR("GetProcessedSamples: Failed to get metric on target {} for handle {}", (*target_or_error)->Name(),
                   params.metric_handle);
    return std::unexpected(metric_or_error.error());
  }

  return SampleCountRequest{
      .target           = *target_or_error,
      .metric           = *metric_or_error,
      .out_sample_count = params.sample_count,
  };
}

auto PopulateSampleCountOutput(const SampleCountRequest& request, uint64_t start_ts, uint64_t end_ts,
                               const char* overflow_log_source) -> astl_status_code {
  const auto samples_or_error = GetProcessedMetricSamples(request.metric, request.target);
  if (!samples_or_error) {
    return samples_or_error.error();
  }

  const auto filtered_count = CountSamplesInTimestampRange(*samples_or_error, start_ts, end_ts);
  if (filtered_count > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("{} reports absurdly large filtered sample count: {}", overflow_log_source, filtered_count);
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  *request.out_sample_count = static_cast<uint32_t>(filtered_count);
  return ASTL_STATUS_SUCCESS;
}

template <typename ParamsT, typename ParseRequestT>
auto GetSampleCountOnTarget(const ParamsT* params, const char* api_name, ParseRequestT parse_request,
                            const char* overflow_log_source) -> astl_status_code {
  auto status = ValidateTimestampedApiParams(params, api_name);
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = parse_request(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      status = PopulateSampleCountOutput(*request_or_error, params->start_ts, params->end_ts, overflow_log_source);
    }
  }
  return status;
}

struct CounterSamplesRequest {
  const astl::ITarget*     target{nullptr};
  const astl::IMetric*     counter{nullptr};
  std::span<astl_sample_t> output_samples;
  uint32_t*                out_sample_count{nullptr};
};

auto ParseCounterSamplesRequest(const astl_get_counter_samples_on_target_params_t& params)
    -> std::expected<CounterSamplesRequest, astl_status_code> {
  const bool missing_required_ptr = (params.target_handle == nullptr) || (params.counter_handle == nullptr) ||
                                    (params.samples == nullptr) || (params.sample_count == nullptr);
  if (missing_required_ptr) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (*params.sample_count == 0) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto target_result = GetTargetFromHandle(params.target_handle);
  if (!target_result) {
    return std::unexpected(target_result.error());
  }
  auto counter_result = GetCounterFromHandle(params.counter_handle, *target_result);
  if (!counter_result) {
    return std::unexpected(counter_result.error());
  }

  return CounterSamplesRequest{
      .target           = *target_result,
      .counter          = *counter_result,
      .output_samples   = std::span<astl_sample_t>{params.samples, *params.sample_count},
      .out_sample_count = params.sample_count,
  };
}

auto CopyProcessedSamplesToOutput(std::span<const astl::ProcessedSampledData> processed_samples,
                                  std::span<astl_sample_t>                    output_samples) -> void {
  auto to_api_sample = [](const astl::ProcessedSampledData& processed_sample) {
    const auto union_value = processed_sample.value.ToAstlUnionValue().first;
    return astl_sample_t{
        .timestamp = static_cast<uint64_t>(processed_sample.timestamp.time_since_epoch().count()),
        .value     = union_value,
    };
  };
  std::transform(processed_samples.begin(), processed_samples.end(), output_samples.begin(), to_api_sample);
}

auto PopulateCounterSamplesOutput(const CounterSamplesRequest& request, uint64_t start_ts, uint64_t end_ts)
    -> astl_status_code {
  auto sample_result = GetProcessedMetricSamples(request.counter, request.target);
  if (!sample_result) {
    return sample_result.error();
  }

  std::vector<astl::ProcessedSampledData> filtered_storage;
  auto filtered_samples = std::span<const astl::ProcessedSampledData>(*sample_result);
  if (start_ts != 0 || end_ts != 0) {
    filtered_storage = FilterSamplesInTimestampRange(*sample_result, start_ts, end_ts);
    filtered_samples = filtered_storage;
  }

  if (filtered_samples.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("astlGetCounterSamplesOnTarget: filtered sample count exceeds uint32_t max: {}",
                   filtered_samples.size());
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  *request.out_sample_count = static_cast<uint32_t>(filtered_samples.size());
  if (filtered_samples.size() > request.output_samples.size()) {
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  CopyProcessedSamplesToOutput(filtered_samples, request.output_samples);
  return ASTL_STATUS_SUCCESS;
}

struct MetricSamplesRequest {
  const astl::ITarget*     target{nullptr};
  const astl::IMetric*     metric{nullptr};
  std::span<astl_sample_t> output_samples;
  uint32_t*                out_sample_count{nullptr};
};

auto ParseMetricSamplesRequest(const astl_get_metric_samples_on_target_params_t& params)
    -> std::expected<MetricSamplesRequest, astl_status_code> {
  const bool missing_required_ptr = (params.target_handle == nullptr) || (params.metric_handle == nullptr) ||
                                    (params.samples == nullptr) || (params.sample_count == nullptr);
  if (missing_required_ptr) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (*params.sample_count == 0) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto target_result = GetTargetFromHandle(params.target_handle);
  if (!target_result) {
    return std::unexpected(target_result.error());
  }
  auto metric_result = GetMetricFromHandle(params.metric_handle, params.target_handle);
  if (!metric_result) {
    return std::unexpected(metric_result.error());
  }

  return MetricSamplesRequest{
      .target           = *target_result,
      .metric           = *metric_result,
      .output_samples   = std::span<astl_sample_t>{params.samples, *params.sample_count},
      .out_sample_count = params.sample_count,
  };
}

auto ValidateMetricSamplesOutputCapacity(const MetricSamplesRequest&                 request,
                                         std::span<const astl::ProcessedSampledData> collected_samples)
    -> astl_status_code {
  if (request.output_samples.size() >= collected_samples.size()) {
    return ASTL_STATUS_SUCCESS;
  }
  if (collected_samples.size() > std::numeric_limits<uint32_t>::max()) {
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  *request.out_sample_count = static_cast<uint32_t>(collected_samples.size());
  return ASTL_STATUS_BUFFER_TOO_SMALL;
}

struct MetricHistogramOutputRequest {
  astl_target_handle_t                     target_handle{nullptr};
  astl_metric_handle_t                     metric_handle{nullptr};
  std::span<astl_discrete_histogram_bin_t> bins;
  uint32_t*                                out_bin_count{nullptr};
};

auto ParseMetricHistogramOutputRequest(const astl_get_metric_discrete_histogram_on_target_params_t& params)
    -> std::expected<MetricHistogramOutputRequest, astl_status_code> {
  if (!params.target_handle || !params.metric_handle || !params.bins || !params.bin_count) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: Invalid argument(s)");
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (*params.bin_count == 0) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: bin_count must be > 0");
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto       bins_span                   = std::span<astl_discrete_histogram_bin_t>{params.bins, *params.bin_count};
  const auto histogram_bin_struct_status = GetStructVersionStatus(bins_span.front());
  if (histogram_bin_struct_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: Invalid bin struct size: {} (expected {})",
                   bins_span.front().size, sizeof(astl_discrete_histogram_bin_t));
    return std::unexpected(histogram_bin_struct_status);
  }

  return MetricHistogramOutputRequest{
      .target_handle = params.target_handle,
      .metric_handle = params.metric_handle,
      .bins          = bins_span,
      .out_bin_count = params.bin_count,
  };
}

auto PopulateDiscreteHistogramBins(const astl::HistogramSummary&            histogram,
                                   std::span<astl_discrete_histogram_bin_t> bins, uint32_t* out_bin_count)
    -> astl_status_code {
  const auto& internal_bins = histogram.bins;
  if (internal_bins.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: bin count exceeds uint32_t max: {}", internal_bins.size());
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto required_count = static_cast<uint32_t>(internal_bins.size());
  if (bins.size() < required_count) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: bin array too small (capacity={}, required={})",
                   bins.size(), required_count);
    *out_bin_count = required_count;
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  for (uint32_t index = 0; index < required_count; ++index) {
    bins[index].size  = sizeof(astl_discrete_histogram_bin_t);
    bins[index].value = internal_bins[index].value.ToAstlUnionValue().first;
    bins[index].count = static_cast<uint64_t>(internal_bins[index].count);
  }

  *out_bin_count = required_count;
  return ASTL_STATUS_SUCCESS;
}

struct SelectedSystemInfoData {
  const astl::PlatformInfoData*                 info{nullptr};
  std::shared_ptr<const astl::PlatformInfoData> loaded_info_ref;
  uint32_t                                      selected_flag{0U};
};

auto ValidateSystemInfoRequest(astl_platform_props_t* system_info) noexcept
    -> std::expected<uint32_t, astl_status_code> {
  if (system_info == nullptr) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  const auto system_info_struct_status = GetStructVersionStatus(*system_info);
  if (system_info_struct_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(system_info_struct_status);
  }

  const uint32_t request_flags          = system_info->flags;
  const uint32_t k_allowed_source_flags = (ASTL_SYSTEM_INFO_FLAG_HOST | ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION);
  if ((request_flags & ~k_allowed_source_flags) != 0U) {
    return std::unexpected(ASTL_STATUS_INVALID_FLAG_VALUE);
  }
  if ((request_flags & ASTL_SYSTEM_INFO_FLAG_HOST) != 0U &&
      (request_flags & ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION) != 0U) {
    return std::unexpected(ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  return request_flags;
}

auto ResolveSystemInfoSource(uint32_t request_flags) noexcept
    -> std::expected<SelectedSystemInfoData, astl_status_code> {
  SelectedSystemInfoData resolved;
  if ((request_flags & ASTL_SYSTEM_INFO_FLAG_HOST) != 0U) {
    resolved.info          = &astl::GetHostPlatformInfo();
    resolved.selected_flag = ASTL_SYSTEM_INFO_FLAG_HOST;
    return resolved;
  }
  if ((request_flags & ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION) != 0U) {
    resolved.loaded_info_ref = astl::GetLoadedPlatformInfo();
    resolved.info            = resolved.loaded_info_ref.get();
    if (resolved.info == nullptr) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    resolved.selected_flag = ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
    return resolved;
  }

  resolved.loaded_info_ref = astl::GetLoadedPlatformInfo();
  if (resolved.loaded_info_ref != nullptr) {
    resolved.info          = resolved.loaded_info_ref.get();
    resolved.selected_flag = ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
    return resolved;
  }

  resolved.info          = &astl::GetHostPlatformInfo();
  resolved.selected_flag = ASTL_SYSTEM_INFO_FLAG_HOST;
  return resolved;
}

auto PopulateSystemInfo(astl_platform_props_t* system_info, const astl::PlatformInfoData& info, uint32_t selected_flag)
    -> void {
  system_info->flags            = selected_flag;
  system_info->soc_name         = info.soc_name.empty() ? nullptr : info.soc_name.c_str();
  system_info->vendor_id        = info.vendor_id.empty() ? nullptr : info.vendor_id.c_str();
  system_info->os_name          = info.os_name.empty() ? nullptr : info.os_name.c_str();
  system_info->kernel_name      = info.kernel_name.empty() ? nullptr : info.kernel_name.c_str();
  system_info->kernel_version   = info.kernel_version.empty() ? nullptr : info.kernel_version.c_str();
  system_info->kernel_release   = info.kernel_release.empty() ? nullptr : info.kernel_release.c_str();
  system_info->firmware_version = info.firmware_version.empty() ? nullptr : info.firmware_version.c_str();
  system_info->hostname         = info.hostname.empty() ? nullptr : info.hostname.c_str();
  system_info->architecture     = info.architecture.empty() ? nullptr : info.architecture.c_str();
}

auto GetMetricHandlesInGroupForTarget(astl::IMetricManager* metric_manager, astl_metric_group_handle_t group,
                                      const astl::ITarget* target) noexcept
    -> std::expected<std::vector<astl_metric_handle_t>, astl_status_code> {
  if (metric_manager == nullptr || target == nullptr) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  auto metrics_in_group = metric_manager->GetMetricsInGroup(group);
  if (!metrics_in_group) {
    return std::unexpected(metrics_in_group.error());
  }

  std::vector<astl_metric_handle_t> filtered_metrics;
  filtered_metrics.reserve(metrics_in_group->size());
  for (const auto* const metric_handle : *metrics_in_group) {
    auto metric_on_target = metric_manager->GetMetricOnTarget(metric_handle, target);
    if (metric_on_target) {
      filtered_metrics.push_back(metric_handle);
      continue;
    }
    if (metric_on_target.error() != ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET) {
      return std::unexpected(metric_on_target.error());
    }
  }

  return filtered_metrics;
}

/***********************************************************************************
 **********************            SYSTEM PROPERTIES         ************************
 **********************************************************************************/

auto astlGetSystemInfo(const astl_get_system_info_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }

  auto request_flags_or_error = ValidateSystemInfoRequest(params->system_info);
  if (!request_flags_or_error) {
    return request_flags_or_error.error();
  }

  auto system_info_or_error = ResolveSystemInfoSource(*request_flags_or_error);
  if (!system_info_or_error) {
    return system_info_or_error.error();
  }

  PopulateSystemInfo(params->system_info, *system_info_or_error->info, system_info_or_error->selected_flag);

  return ASTL_STATUS_SUCCESS;
}

/***********************************************************************************
 **********************               TARGETS               ************************
 **********************************************************************************/

auto astlGetTargetCount(const astl_get_target_count_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto* target_count = params->target_count;
  if (!target_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();

  auto targets_size = orchestrator->GetTargets().size();
  // error if the number of targets won't fit in a 32-bit integer (unlikely)
  if (targets_size > std::numeric_limits<uint32_t>::max()) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  *target_count = static_cast<uint32_t>(targets_size);
  return ASTL_STATUS_SUCCESS;
}

using VersionedPropertiesSpan = std::variant<std::span<astl_target_props_t>>;

/**
 * @brief Retrieve either an error code or a span of astl_target_props_t.
 */
auto GetVersionedTargetPropertiesSpan(astl_target_props_t* targets, uint32_t target_count) noexcept
    -> std::expected<VersionedPropertiesSpan, astl_status_code> {
  // at first, assume the caller's targets are the same size as the astl_target_props_t struct in our header.
  std::span<astl_target_props_t> target_span{targets, target_count};
  auto                           given_struct_size = GetFirstElementSizeField(target_span);
  if (!given_struct_size) {
    return std::unexpected(given_struct_size.error());
  }
  switch (*given_struct_size) {
    case sizeof(astl_target_props_t):
      return target_span;
    default:
      const auto target_struct_status = GetStructVersionStatus(target_span.front());
      return std::unexpected(target_struct_status);
  }
}

struct GetTargetsRequest {
  astl_target_props_t* targets{nullptr};
  uint32_t*            target_count{nullptr};
};

auto ValidateGetTargetsRequest(const astl_get_targets_params_t* params) noexcept
    -> std::expected<GetTargetsRequest, astl_status_code> {
  if (params->targets == nullptr || params->target_count == nullptr || *params->target_count == 0) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  return GetTargetsRequest{.targets = params->targets, .target_count = params->target_count};
}

auto ValidateTargetCapacity(size_t available_targets_count, uint32_t* target_count) noexcept -> astl_status_code {
  if (available_targets_count == 0) {
    *target_count = 0;
    return ASTL_STATUS_NO_TARGET_FOUND;
  }
  if (available_targets_count > *target_count) {
    if (available_targets_count <= std::numeric_limits<uint32_t>::max()) {
      *target_count = static_cast<uint32_t>(available_targets_count);
    }
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }
  return ASTL_STATUS_SUCCESS;
}

auto PopulateTargetProperties(const VersionedPropertiesSpan&                  target_span,
                              std::span<const std::unique_ptr<astl::ITarget>> available_targets,
                              uint32_t* target_count) noexcept -> astl_status_code {
  const auto populate_properties = [available_targets, target_count](auto&& target_properties) {
    for (size_t i = 0; i < std::min(available_targets.size(), target_properties.size()); ++i) {
      auto result = available_targets[i]->GetProperties(&target_properties[i]);
      if (result != ASTL_STATUS_SUCCESS) {
        *target_count = static_cast<uint32_t>(i);
        return result;
      }
    }
    *target_count = static_cast<uint32_t>(available_targets.size());
    return available_targets.size() == target_properties.size() ? ASTL_STATUS_SUCCESS
                                                                : ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED;
  };

  try {
    return std::visit(populate_properties, target_span);
  } catch (const std::bad_variant_access& e) {
    ASTL_LOG_ERROR("astlGetTargets: bad_variant_access exception: {}", e.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
}

auto astlGetTargets(const astl_get_targets_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    auto request_or_error = ValidateGetTargetsRequest(params);
    if (!request_or_error) {
      return request_or_error.error();
    }

    auto*       targets               = request_or_error->targets;
    auto*       target_count          = request_or_error->target_count;
    auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
    if (!orchestrator_or_error) {
      return orchestrator_or_error.error();
    }
    const auto& orchestrator = orchestrator_or_error->get();

    auto const& available_targets       = orchestrator->GetTargets();
    auto        available_targets_count = available_targets.size();

    const auto target_capacity_status = ValidateTargetCapacity(available_targets_count, target_count);
    if (target_capacity_status != ASTL_STATUS_SUCCESS) {
      return target_capacity_status;
    }

    // convert our raw pointer to _some_ version of astl_target_props_t into a std::span
    // of a specific struct, maybe astl_target_props_t, maybe astl_target_properties_v0_t,
    // or just an error if we can't support the given struct size.
    auto target_span = GetVersionedTargetPropertiesSpan(targets, *target_count);
    if (!target_span) {
      return target_span.error();
    }

    return PopulateTargetProperties(*target_span, available_targets, target_count);
  }();
}

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/
// TODO(ASTL-180) counter should be re-implemented as just a RawMetric.
auto astlGetCounterCountOnTarget(const astl_get_counter_count_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* target_handle = params->target_handle;
    auto*       counter_count = params->counter_count;
    if (!target_handle || !counter_count) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    auto get_metric_manager_result = GetMetricManager();
    if (!get_metric_manager_result) {
      return get_metric_manager_result.error();
    }
    auto* metric_manager = *get_metric_manager_result;

    auto result = GetTargetFromHandle(target_handle);
    if (!result) {
      return result.error();
    }
    const auto* target       = *result;
    const auto  num_counters = metric_manager->GetNumAvailableCounters(target);
    if (num_counters > std::numeric_limits<uint32_t>::max()) {
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    *counter_count = static_cast<uint32_t>(num_counters);
    return ASTL_STATUS_SUCCESS;
  }();
}

auto astlGetCountersOnTarget(const astl_get_counters_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* target_handle = params->target_handle;
    auto*       counters      = params->counters;
    auto*       counter_count = params->counter_count;
    if (!target_handle || !counters || !counter_count || *counter_count == 0) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }

    const auto                      counters_buffer_size = *counter_count;
    std::span<astl_counter_props_t> output_counters{counters, counters_buffer_size};
    *counter_count = 0;  // in case there's an error to return

    const auto resolved_components = ResolveTargetAndMetricManager(target_handle);
    if (!resolved_components) {
      return resolved_components.error();
    }
    auto*       metric_manager = resolved_components->metric_manager;
    const auto* target         = resolved_components->target;

    const auto available_counters_result = metric_manager->GetAvailableCounters(target);
    if (!available_counters_result) {
      return available_counters_result.error();
    }
    const auto& available_counters = *available_counters_result;

    auto status = ValidateAvailableHandleCount(available_counters, counters_buffer_size, counter_count,
                                               ASTL_STATUS_NO_COUNTERS_FOUND);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }

    status = ValidateCounterOutputStruct(output_counters);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }

    status = PopulateCounterPropertiesForHandles(metric_manager, available_counters, output_counters, counter_count);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }

    *counter_count = static_cast<uint32_t>(available_counters.size());
    return available_counters.size() == counters_buffer_size ? ASTL_STATUS_SUCCESS
                                                             : ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED;
  }();
}

/***********************************************************************************
 **********************              METRIC                    *********************
 **********************************************************************************/

auto astlGetMetricCountOnTarget(const astl_get_metric_count_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* target_handle = params->target_handle;
    auto*       metric_count  = params->metric_count;
    if (!target_handle || !metric_count) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    auto get_metric_manager_result = GetMetricManager();
    if (!get_metric_manager_result) {
      return get_metric_manager_result.error();
    }
    auto* metric_manager    = *get_metric_manager_result;
    auto  get_target_result = GetTargetFromHandle(target_handle);
    if (!get_target_result) {
      return get_target_result.error();
    }
    const auto* target      = *get_target_result;
    const auto  num_metrics = metric_manager->GetNumAvailableMetrics(target);
    if (num_metrics > std::numeric_limits<uint32_t>::max()) {
      ASTL_LOG_ERROR("metric_manager->GetMetrics reports absurdly large number of metrics: {}", num_metrics);
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    *metric_count = static_cast<uint32_t>(num_metrics);
    return ASTL_STATUS_SUCCESS;
  }();
}

auto astlGetMetricsOnTarget(const astl_get_metrics_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }

  auto request_or_error = ParseMetricsOnTargetRequest(*params);
  if (!request_or_error) {
    return request_or_error.error();
  }

  return PopulateMetricsOnTargetResponse(*request_or_error);
}

/***********************************************************************************
 **********************      METRIC STATE DISCOVERY      *********************
 **********************************************************************************/

auto astlGetMetricStateCountOnTarget(const astl_get_metric_state_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       state_count   = params->state_count;
  if (!target_handle || !metric_handle || !state_count) {
    ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Invalid argument(s)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  *state_count = 0;

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  auto metric_or_error = metric_manager->GetMetricOnTarget(metric_handle, target);
  if (!metric_or_error.has_value()) {
    ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Failed to get metric on target {}", target->Name());
    return metric_or_error.error();
  }
  const auto* metric = *metric_or_error;

  // Get metric properties to determine the metric type
  astl_metric_props_t properties{};
  properties.size = sizeof(astl_metric_props_t);
  auto status     = metric->GetProperties(&properties);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Failed to get metric properties");
    return status;
  }

  size_t count = 0;

  // Handle finite set metrics
  if (properties.metric_type == ASTL_METRIC_FINITE_SET_VALUE) {
    const auto* finite_set_metric = dynamic_cast<const astl::FiniteSetMetric*>(metric);
    if (!finite_set_metric) {
      ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Failed to cast to FiniteSetMetric");
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    const auto* finite_set_config = finite_set_metric->GetFiniteSetConfiguration();
    if (!finite_set_config) {
      ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Failed to get FiniteSetMetricConfig");
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    count = finite_set_config->GetStateInfo().size();
  }
  // Handle residency metrics
  else if (properties.metric_type == ASTL_METRIC_RESIDENCY) {
    const auto* residency_metric = dynamic_cast<const astl::ResidencyMetric*>(metric);
    if (!residency_metric) {
      ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Failed to cast to ResidencyMetric");
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    const auto* residency_config = residency_metric->GetResidencyConfiguration();
    if (!residency_config) {
      ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Failed to get ResidencyMetricConfig");
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    count = residency_metric->GetStateConfigs().size() + (residency_config->InferredState().has_value() ? 1 : 0);
  } else {
    ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: Metric {} is neither a finite set nor residency metric",
                   properties.name);
    return ASTL_STATUS_NOT_SUPPORTED;
  }

  if (count > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("astlGetMetricStateCountOnTarget: State count exceeds uint32_t maximum");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  *state_count = static_cast<uint32_t>(count);
  return ASTL_STATUS_SUCCESS;
}

namespace {

auto PopulateFiniteSetStateNames(const astl::IMetric* metric, std::span<astl_state_props_t> output_states,
                                 uint32_t* state_count) noexcept -> astl_status_code {
  const auto* finite_set_metric = dynamic_cast<const astl::FiniteSetMetric*>(metric);
  if (!finite_set_metric) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Failed to cast to FiniteSetMetric");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto* finite_set_config = finite_set_metric->GetFiniteSetConfiguration();
  if (!finite_set_config) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Failed to get FiniteSetMetricConfig");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto& state_info = finite_set_config->GetStateInfo();

  if (state_info.size() > output_states.size()) {
    if (state_info.size() > std::numeric_limits<uint32_t>::max()) {
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    *state_count = static_cast<uint32_t>(state_info.size());
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  size_t index = 0;
  for (const auto& [astl_value, info] : state_info) {
    if (index >= output_states.size()) {
      break;
    }

    output_states[index].size        = sizeof(astl_state_props_t);
    output_states[index].value       = astl_value.ToAstlUnionValue().first;
    output_states[index].name        = astl::GetInternedString(info.state_name);
    output_states[index].description = astl::GetInternedString(info.state_description);
    ++index;
  }

  *state_count = static_cast<uint32_t>(index);
  return ASTL_STATUS_SUCCESS;
}

auto PopulateResidencyStateNames(const astl::IMetric* metric, std::span<astl_state_props_t> output_states,
                                 uint32_t* state_count) noexcept -> astl_status_code {
  const auto* residency_metric = dynamic_cast<const astl::ResidencyMetric*>(metric);
  if (!residency_metric) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Failed to cast to ResidencyMetric");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto* residency_config = residency_metric->GetResidencyConfiguration();
  if (!residency_config) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Failed to get ResidencyMetricConfig");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto& state_configs  = residency_metric->GetStateConfigs();
  const auto& inferred_state = residency_config->InferredState();

  size_t total_state_count = state_configs.size() + (inferred_state.has_value() ? 1 : 0);
  if (total_state_count > output_states.size()) {
    if (total_state_count > std::numeric_limits<uint32_t>::max()) {
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    *state_count = static_cast<uint32_t>(total_state_count);
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  size_t index = 0;
  for (const auto& state_config : state_configs) {
    if (index >= output_states.size()) {
      break;
    }
    output_states[index].size        = sizeof(astl_state_props_t);
    output_states[index].name        = astl::GetInternedString(state_config.state_name);
    output_states[index].description = astl::GetInternedString(state_config.state_description);
    // cppcheck-suppress unreadVariable
    output_states[index].value = {};  // Zero-initialize unused field for residency metrics
    ++index;
  }

  if (inferred_state.has_value() && index < output_states.size()) {
    output_states[index].size = sizeof(astl_state_props_t);
    // cppcheck-suppress unreadVariable
    output_states[index].name        = astl::GetInternedString(inferred_state->name);
    output_states[index].description = astl::GetInternedString(inferred_state->description);
    // cppcheck-suppress unreadVariable
    output_states[index].value = {};  // Zero-initialize unused field for residency metrics
    ++index;
  }

  *state_count = static_cast<uint32_t>(index);
  return ASTL_STATUS_SUCCESS;
}

}  // namespace

auto astlGetMetricStatesOnTarget(const astl_get_metric_states_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       states        = params->states;
  auto*       state_count   = params->state_count;
  if (!target_handle || !metric_handle || !states || !state_count || *state_count == 0) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Invalid argument(s)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  auto metric_or_error = metric_manager->GetMetricOnTarget(metric_handle, target);
  if (!metric_or_error.has_value()) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Failed to get metric on target {}", target->Name());
    return metric_or_error.error();
  }
  const auto* metric = *metric_or_error;

  // Get metric properties to determine the metric type
  astl_metric_props_t properties{};
  properties.size  = sizeof(astl_metric_props_t);
  auto prop_status = metric->GetProperties(&properties);
  if (prop_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Failed to get metric properties");
    return prop_status;
  }

  std::span<astl_state_props_t> output_states{states, *state_count};

  // Check struct size for versioning
  auto state_struct_size = GetFirstElementSizeField(output_states);
  if (!state_struct_size) {
    return state_struct_size.error();
  }
  const auto state_struct_version_status = GetStructVersionStatus(output_states.front());
  if (state_struct_version_status != ASTL_STATUS_SUCCESS) {
    return state_struct_version_status;
  }

  // Delegate to appropriate handler based on metric type
  if (properties.metric_type == ASTL_METRIC_FINITE_SET_VALUE) {
    return PopulateFiniteSetStateNames(metric, output_states, state_count);
  }

  if (properties.metric_type == ASTL_METRIC_RESIDENCY) {
    return PopulateResidencyStateNames(metric, output_states, state_count);
  }

  ASTL_LOG_ERROR("astlGetMetricStatesOnTarget: Metric {} is neither a finite set nor residency metric",
                 properties.name);
  return ASTL_STATUS_NOT_SUPPORTED;
}

/***********************************************************************************
 **********************              METRIC GROUPS             *********************
 **********************************************************************************/

auto astlGetMetricGroupCount(const astl_get_metric_group_count_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto* metric_group_count = params->metric_group_count;
  if (!metric_group_count) {
    ASTL_LOG_ERROR("astlGetMetricGroupCount: metric_group_count is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  ASTL_LOG_TRACE("astlGetMetricGroupCount: getting metric manager");
  *metric_group_count            = 0;
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  ASTL_LOG_TRACE("astlGetMetricGroupCount: getting available metric groups");
  const auto available_metric_groups = metric_manager->GetMetricGroups();
  if (available_metric_groups.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("metric_manager->GetMetricGroups reports absurdly large number of metric groups: {}",
                   available_metric_groups.size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  *metric_group_count = static_cast<uint32_t>(available_metric_groups.size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetMetricGroupCountOnTarget(const astl_get_metric_group_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle      = params->target_handle;
  auto*       metric_group_count = params->metric_group_count;
  if (!target_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupCountOnTarget: target_handle is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_group_count) {
    ASTL_LOG_ERROR("astlGetMetricGroupCountOnTarget: metric_group_count is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  ASTL_LOG_TRACE("astlGetMetricGroupCountOnTarget: getting metric manager");
  *metric_group_count            = 0;
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  ASTL_LOG_TRACE("astlGetMetricGroupCountOnTarget: getting target from handle");
  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  ASTL_LOG_TRACE("astlGetMetricGroupCountOnTarget: getting available metric groups");
  const auto available_metric_groups = metric_manager->GetMetricGroups(target);
  if (!available_metric_groups) {
    return available_metric_groups.error();
  }
  if (available_metric_groups->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("metric_manager->GetMetricGroups reports absurdly large number of metric groups: {}",
                   available_metric_groups->size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  *metric_group_count = static_cast<uint32_t>(available_metric_groups->size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetMetricGroupsOnTarget(const astl_get_metric_groups_on_target_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }

    if (!params->target_handle) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    auto output_or_error = ParseMetricGroupOutputRequest(params->metric_groups, params->metric_group_count);
    if (!output_or_error) {
      return output_or_error.error();
    }

    const auto resolved_components = ResolveTargetAndMetricManager(params->target_handle);
    if (!resolved_components) {
      return resolved_components.error();
    }
    auto*       metric_manager = resolved_components->metric_manager;
    const auto* target         = resolved_components->target;

    auto groups_result = metric_manager->GetMetricGroups(target);
    if (!groups_result) {
      return groups_result.error();
    }
    return PopulateMetricGroupPropertiesForHandles(metric_manager, *groups_result, output_or_error->output,
                                                   output_or_error->out_count);
  }();
}

auto astlGetMetricGroups(const astl_get_metric_groups_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }

  auto output_or_error = ParseMetricGroupOutputRequest(params->metric_groups, params->metric_group_count);
  if (!output_or_error) {
    return output_or_error.error();
  }

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto*      metric_manager = *get_metric_manager_result;
  const auto groups_result  = metric_manager->GetMetricGroups();
  return PopulateMetricGroupPropertiesForHandles(metric_manager, groups_result, output_or_error->output,
                                                 output_or_error->out_count);
}

auto astlGetMetricGroupMetricCount(const astl_get_metric_group_metric_count_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* const metric_group_handle = params->metric_group_handle;
  auto*             metric_count        = params->metric_count;
  if (!metric_group_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricCount: metric_group_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_count) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricCount: metric_count ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  auto metrics_in_group = metric_manager->GetMetricsInGroup(metric_group_handle);
  if (!metrics_in_group) {
    return metrics_in_group.error();
  }
  if (metrics_in_group->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("GetMetricsInGroup reports absurdly large number of metrics: {}", metrics_in_group->size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  *metric_count = static_cast<uint32_t>(metrics_in_group->size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetMetricGroupMetrics(const astl_get_metric_group_metrics_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }

    if (!params->metric_group_handle) {
      ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metric_group_handle cannot be null");
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    auto output_or_error = ParseMetricPropertiesOutputRequest(params->metrics, params->metric_count);
    if (!output_or_error) {
      ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metrics/metric_count are invalid");
      return output_or_error.error();
    }

    auto get_metric_manager_result = GetMetricManager();
    if (!get_metric_manager_result) {
      return get_metric_manager_result.error();
    }
    auto* metric_manager = *get_metric_manager_result;

    ASTL_LOG_TRACE("astlGetMetricGroupMetrics: getting metrics in group");
    auto metrics_in_group = metric_manager->GetMetricsInGroup(params->metric_group_handle);
    if (!metrics_in_group) {
      return metrics_in_group.error();
    }

    return PopulateMetricPropertiesForMetricGroup(metric_manager, *metrics_in_group, output_or_error->output,
                                                  output_or_error->out_count);
  }();
}

auto astlGetMetricGroupMetricCountOnTarget(const astl_get_metric_group_metric_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto*       target_handle       = params->target_handle;
  const auto* const metric_group_handle = params->metric_group_handle;
  auto*             metric_count        = params->metric_count;
  if (!target_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricCountOnTarget: target_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_group_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricCountOnTarget: metric_group_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_count) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricCountOnTarget: metric_count ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager    = *get_metric_manager_result;
  auto  get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  auto metrics_in_group = GetMetricHandlesInGroupForTarget(metric_manager, metric_group_handle, target);
  if (!metrics_in_group) {
    return metrics_in_group.error();
  }
  if (metrics_in_group->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("GetMetricHandlesInGroupForTarget reports absurdly large number of metrics: {}",
                   metrics_in_group->size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  *metric_count = static_cast<uint32_t>(metrics_in_group->size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetMetricGroupMetricsOnTarget(const astl_get_metric_group_metrics_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto*       target_handle       = params->target_handle;
  const auto* const metric_group_handle = params->metric_group_handle;
  auto*             metrics             = params->metrics;
  auto*             metric_count        = params->metric_count;
  if (!target_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricsOnTarget: target_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_group_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricsOnTarget: metric_group_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_count) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricsOnTarget: metric_count ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metrics) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricsOnTarget: metrics ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager    = *get_metric_manager_result;
  auto  get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  ASTL_LOG_TRACE("astlGetMetricGroupMetricsOnTarget: getting metrics in group");
  auto metrics_in_group = GetMetricHandlesInGroupForTarget(metric_manager, metric_group_handle, target);
  if (!metrics_in_group) {
    return metrics_in_group.error();
  }
  if (metrics_in_group->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("GetMetricHandlesInGroupForTarget reports absurdly large number of metrics: {}",
                   metrics_in_group->size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto required_metric_count = static_cast<uint32_t>(metrics_in_group->size());
  if (*metric_count == 0) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetricsOnTarget: metric_count cannot be 0 when metrics ptr is non-null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::span<astl_metric_props_t> metrics_properties{metrics, *metric_count};
  if (metrics_properties.size() < metrics_in_group->size()) {
    *metric_count = required_metric_count;
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }

  auto status = PopulateMetricPropertiesForHandles(metric_manager, *metrics_in_group,
                                                   metrics_properties.first(metrics_in_group->size()));
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  *metric_count = required_metric_count;
  return metrics_properties.size() > metrics_in_group->size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
                                                              : ASTL_STATUS_SUCCESS;
}

namespace {

auto GetKnownMetricGroups(astl::IMetricManager* metric_manager) -> std::unordered_set<astl_metric_group_handle_t> {
  const auto global_groups = metric_manager->GetMetricGroups();
  return {global_groups.begin(), global_groups.end()};
}

auto GetTargetMetricGroups(astl::IMetricManager* metric_manager, const astl::ITarget* target)
    -> std::expected<std::unordered_set<astl_metric_group_handle_t>, astl_status_code> {
  std::unordered_set<astl_metric_group_handle_t> target_groups;
  if (target == nullptr) {
    return target_groups;
  }

  auto target_groups_result = metric_manager->GetMetricGroups(target);
  if (!target_groups_result) {
    return std::unexpected{target_groups_result.error()};
  }
  target_groups.insert(target_groups_result->begin(), target_groups_result->end());
  return target_groups;
}

auto ValidateRequestedGroupOnTarget(astl_metric_group_handle_t group_handle, const astl::ITarget* target,
                                    const std::unordered_set<astl_metric_group_handle_t>& known_groups,
                                    const std::unordered_set<astl_metric_group_handle_t>& target_groups,
                                    bool fail_if_group_missing_on_target) -> std::expected<bool, astl_status_code> {
  if (!known_groups.contains(group_handle)) {
    return std::unexpected{ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE};
  }
  if (target == nullptr || target_groups.contains(group_handle)) {
    return true;
  }
  if (fail_if_group_missing_on_target) {
    return std::unexpected{ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET};
  }
  return false;
}

auto GetMetricHandlesForGroup(astl::IMetricManager* metric_manager, const astl::ITarget* target,
                              astl_metric_group_handle_t group_handle, bool fail_if_group_missing_on_target)
    -> std::expected<std::vector<astl_metric_handle_t>, astl_status_code> {
  if (target == nullptr) {
    auto global_group_metrics = metric_manager->GetMetricsInGroup(group_handle);
    if (!global_group_metrics) {
      return std::unexpected{global_group_metrics.error()};
    }
    return std::vector<astl_metric_handle_t>{global_group_metrics->begin(), global_group_metrics->end()};
  }

  auto group_metrics = GetMetricHandlesInGroupForTarget(metric_manager, group_handle, target);
  if (!group_metrics) {
    return std::unexpected{group_metrics.error()};
  }
  if (group_metrics->empty() && fail_if_group_missing_on_target) {
    return std::unexpected{ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET};
  }
  return group_metrics;
}

auto AppendUniqueMetricHandles(std::vector<astl_metric_handle_t>&        metric_handles,
                               std::unordered_set<astl_metric_handle_t>& seen_metric_handles,
                               std::span<const astl_metric_handle_t>     group_metrics) -> void {
  if (metric_handles.capacity() < metric_handles.size() + group_metrics.size()) {
    const auto new_size = std::max(metric_handles.capacity() * 2, metric_handles.size() + group_metrics.size());
    metric_handles.reserve(new_size);
  }
  std::copy_if(group_metrics.begin(), group_metrics.end(), std::back_inserter(metric_handles),
               [&](const auto& metric_handle) { return seen_metric_handles.insert(metric_handle).second; });
}

}  // namespace

auto ExpandMetricGroupHandlesForTarget(astl::IMetricManager* metric_manager, const astl::ITarget* target,
                                       std::span<const astl_metric_group_handle_t> requested_groups,
                                       bool                                        fail_if_group_missing_on_target)
    -> std::expected<std::vector<astl_metric_handle_t>, astl_status_code> {
  if (!metric_manager) {
    return std::unexpected{ASTL_STATUS_INTERNAL_ERROR};
  }

  const auto known_groups_or_error  = GetKnownMetricGroups(metric_manager);
  auto       target_groups_or_error = GetTargetMetricGroups(metric_manager, target);
  if (!target_groups_or_error) {
    return std::unexpected{target_groups_or_error.error()};
  }

  std::vector<astl_metric_handle_t>        metric_handles;
  std::unordered_set<astl_metric_handle_t> seen_metric_handles;
  const auto&                              known_groups  = known_groups_or_error;
  const auto&                              target_groups = *target_groups_or_error;

  for (const auto& group_handle : requested_groups) {
    auto requested_group_is_supported = ValidateRequestedGroupOnTarget(group_handle, target, known_groups,
                                                                       target_groups, fail_if_group_missing_on_target);
    if (!requested_group_is_supported) {
      return std::unexpected{requested_group_is_supported.error()};
    }
    if (!*requested_group_is_supported) {
      continue;
    }

    auto group_metrics =
        GetMetricHandlesForGroup(metric_manager, target, group_handle, fail_if_group_missing_on_target);
    if (!group_metrics) {
      return std::unexpected{group_metrics.error()};
    }
    if (group_metrics->empty()) {
      continue;
    }

    AppendUniqueMetricHandles(metric_handles, seen_metric_handles, *group_metrics);
  }

  return metric_handles;
}

auto DeduplicateMetricHandles(std::span<const astl_metric_handle_t> metric_handles)
    -> std::vector<astl_metric_handle_t> {
  std::vector<astl_metric_handle_t>        deduplicated_handles;
  std::unordered_set<astl_metric_handle_t> seen_metric_handles;
  deduplicated_handles.reserve(metric_handles.size());

  std::copy_if(metric_handles.begin(), metric_handles.end(), std::back_inserter(deduplicated_handles),
               [&](const auto& metric_handle) { return seen_metric_handles.insert(metric_handle).second; });

  return deduplicated_handles;
}

/***********************************************************************************
 **********************              COLLECTION                *********************
 **********************************************************************************/

namespace {

struct ConfigureCounterCollectionOnTargetRequest {
  const astl::ITarget*                   target{nullptr};
  astl::IMetricManager*                  metric_manager{nullptr};
  const astl_collection_params_t*        collection_params{nullptr};
  std::span<const astl_counter_handle_t> counter_handles;
};

struct MetricGroupCollectionContext {
  astl::Orchestrator*   orchestrator{nullptr};
  astl::IMetricManager* metric_manager{nullptr};
};

auto ResolveMetricGroupCollectionContext() -> std::expected<MetricGroupCollectionContext, astl_status_code> {
  auto orchestrator_or_error = GetOrchestratorInstance();
  if (!orchestrator_or_error) {
    return std::unexpected(orchestrator_or_error.error());
  }

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return std::unexpected(get_metric_manager_result.error());
  }

  return MetricGroupCollectionContext{
      .orchestrator   = *orchestrator_or_error,
      .metric_manager = *get_metric_manager_result,
  };
}

auto ConfigureMetricGroupCollectionOnTarget(const MetricGroupCollectionContext&         context,
                                            const astl_collection_params_t*             collection_params,
                                            std::span<const astl_metric_group_handle_t> metric_group_handles,
                                            const astl::ITarget* target) -> std::expected<bool, astl_status_code> {
  auto metric_handles_or_error =
      ExpandMetricGroupHandlesForTarget(context.metric_manager, target, metric_group_handles, false);
  if (!metric_handles_or_error) {
    return std::unexpected(metric_handles_or_error.error());
  }
  if (metric_handles_or_error->empty()) {
    return false;
  }

  std::span<const astl_metric_handle_t> metric_handle_span{*metric_handles_or_error};
  const auto                            configure_status =
      context.orchestrator->ConfigureMetricCollection(target, collection_params, metric_handle_span);
  if (configure_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(configure_status);
  }
  return true;
}

auto ParseConfigureCounterCollectionOnTargetRequest(const astl_configure_counter_collection_on_target_params_t& params)
    -> std::expected<ConfigureCounterCollectionOnTargetRequest, astl_status_code> {
  return [&]() -> std::expected<ConfigureCounterCollectionOnTargetRequest, astl_status_code> {
    if (!params.target_handle || !params.collection_params || !params.counter_handles || params.counter_count == 0) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    const auto collection_params_struct_status = GetStructVersionStatus(*params.collection_params);
    if (collection_params_struct_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(collection_params_struct_status);
    }
    const auto collection_flags_status = ValidateCollectionParamsFlags(params.collection_params);
    if (collection_flags_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(collection_flags_status);
    }

    auto resolved_components = ResolveTargetAndMetricManager(params.target_handle);
    if (!resolved_components) {
      return std::unexpected(resolved_components.error());
    }
    const auto available_counters =
        resolved_components->metric_manager->GetAvailableCounters(resolved_components->target);
    if (!available_counters) {
      return std::unexpected(available_counters.error());
    }
    if (params.counter_count > available_counters->size()) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }

    return ConfigureCounterCollectionOnTargetRequest{
        .target            = resolved_components->target,
        .metric_manager    = resolved_components->metric_manager,
        .collection_params = params.collection_params,
        .counter_handles   = std::span<const astl_counter_handle_t>{params.counter_handles, params.counter_count},
    };
  }();
}

auto ConfigureMetricGroupCollectionOnAllTargets(const astl_collection_params_t*             collection_params,
                                                std::span<const astl_metric_group_handle_t> metric_group_handles)
    -> astl_status_code {
  auto context_or_error = ResolveMetricGroupCollectionContext();
  if (!context_or_error) {
    return context_or_error.error();
  }

  const auto& targets = context_or_error->orchestrator->GetTargets();
  if (targets.empty()) {
    return ASTL_STATUS_NO_TARGET_FOUND;
  }

  bool any_target_configured = false;
  for (const auto& target_ptr : targets) {
    auto configured_target_or_error = ConfigureMetricGroupCollectionOnTarget(*context_or_error, collection_params,
                                                                             metric_group_handles, target_ptr.get());
    if (!configured_target_or_error) {
      return configured_target_or_error.error();
    }
    any_target_configured = any_target_configured || *configured_target_or_error;
  }

  return any_target_configured ? ASTL_STATUS_SUCCESS : ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET;
}

}  // namespace

/*** CONFIGURE COUNTERS ***/
auto astlConfigureCounterCollectionOnTarget(const astl_configure_counter_collection_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }

  // Any configure* call exits loaded-session system-info mode and returns to host-captured info.
  SwitchSystemInfoToHostCapture();

  auto request_or_error = ParseConfigureCounterCollectionOnTargetRequest(*params);
  if (!request_or_error) {
    return request_or_error.error();
  }

  auto orchestrator_or_error = GetOrchestratorInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  return (*orchestrator_or_error)
      ->ConfigureCounterCollection(request_or_error->target, request_or_error->collection_params,
                                   request_or_error->counter_handles);
}

auto astlConfigureCounterCollection(const astl_configure_counter_collection_params_t* params) noexcept
    -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* collection_params = params->collection_params;
    const auto* counter_handles   = params->counter_handles;
    const auto  counter_count     = params->counter_count;
    if (!collection_params || !counter_handles) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    if (counter_count == 0) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto collection_params_struct_status = GetStructVersionStatus(*collection_params);
    if (collection_params_struct_status != ASTL_STATUS_SUCCESS) {
      return collection_params_struct_status;
    }
    const auto collection_flags_status = ValidateCollectionParamsFlags(collection_params);
    if (collection_flags_status != ASTL_STATUS_SUCCESS) {
      return collection_flags_status;
    }

    SwitchSystemInfoToHostCapture();

    std::vector<const astl::ICounter*>     counters;
    std::span<const astl_counter_handle_t> counter_handle_span{counter_handles, counter_count};
    std::transform(begin(counter_handle_span), std::end(counter_handle_span), std::back_inserter(counters),
                   [](const auto& counter_handle) { return static_cast<const astl::ICounter*>(counter_handle); });

    // TODO(https://jira.arm.com/browse/ASTL-54)  -- the many-to-many association between counters and targets is
    // breaking down here. Current class model has targets owning counters - that doesn't allow for one Counter being
    // accessible from many targets. Perhaps have a Target own a vector<shared_ptr> to its counters? without that, we
    // don't have a good way to verify it's a valid Counter handle, unless we check if any target owns the given
    // counter?
    //
    // To decide: Do we need do make sure the handles of counters the user specified
    //            are applicable to all targets? Of we do we configure each counter on each target where applicable?

    // TODO(https://github.com/Arm-Debug/ASTL/pull/28#discussion_r2052533543)
    // TODO(https://jira.arm.com/browse/ASTL-35) - should we check if the target supports the counters?
    //  - what if target 2 raises an error here - do we unwind the
    //    configuration on others or leave that to user?
    (void)counter_handle_span;  // unused for now since unimplemented
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }();
}

/*** CONFIGURE METRICS ***/
auto astlConfigureMetricCollectionOnTarget(const astl_configure_metric_collection_on_target_params_t* params) noexcept
    -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* target_handle     = params->target_handle;
    const auto* collection_params = params->collection_params;
    const auto* metric_handles    = params->metric_handles;
    const auto  metric_count      = params->metric_count;
    // check input arguments for null and api version
    if (!target_handle || !collection_params || !metric_handles || metric_count == 0) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto collection_params_struct_status = GetStructVersionStatus(*collection_params);
    if (collection_params_struct_status != ASTL_STATUS_SUCCESS) {
      return collection_params_struct_status;
    }
    const auto collection_flags_status = ValidateCollectionParamsFlags(collection_params);
    if (collection_flags_status != ASTL_STATUS_SUCCESS) {
      return collection_flags_status;
    }

    SwitchSystemInfoToHostCapture();

    auto get_target_result = GetTargetFromHandle(target_handle);
    if (!get_target_result) {
      return get_target_result.error();
    }
    const auto* target                      = *get_target_result;
    auto        deduplicated_metric_handles = DeduplicateMetricHandles({metric_handles, metric_count});
    std::span<const astl_metric_handle_t> metric_handle_span{deduplicated_metric_handles};
    auto const&                           orchestrator_or_error = astl::Orchestrator::GetInstance();
    if (!orchestrator_or_error) {
      return orchestrator_or_error.error();
    }
    const auto& orchestrator = orchestrator_or_error->get();
    return orchestrator->ConfigureMetricCollection(target, collection_params, metric_handle_span);
  }();
}

auto astlConfigureMetricCollection(const astl_configure_metric_collection_params_t* params) noexcept
    -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* collection_params = params->collection_params;
    const auto* metric_handles    = params->metric_handles;
    const auto  metric_count      = params->metric_count;
    if (!collection_params || !metric_handles) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    if (metric_count == 0) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto collection_params_struct_status = GetStructVersionStatus(*collection_params);
    if (collection_params_struct_status != ASTL_STATUS_SUCCESS) {
      return collection_params_struct_status;
    }
    const auto collection_flags_status = ValidateCollectionParamsFlags(collection_params);
    if (collection_flags_status != ASTL_STATUS_SUCCESS) {
      return collection_flags_status;
    }
    SwitchSystemInfoToHostCapture();

    (void)metric_handles;
    (void)metric_count;
    astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
    return result;
  }();
}

/*** CONFIGURE METRIC GROUPS ***/
auto astlConfigureMetricGroupCollectionOnTarget(
    const astl_configure_metric_group_collection_on_target_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    const auto                  params_status = ValidateApiParams(params);
    if (params_status != ASTL_STATUS_SUCCESS) {
      return params_status;
    }
    const auto* target_handle        = params->target_handle;
    const auto* collection_params    = params->collection_params;
    const auto* metric_group_handles = params->metric_group_handles;
    const auto  metric_group_count   = params->metric_group_count;
    // check input arguments
    if (!target_handle || !collection_params || !metric_group_handles || metric_group_count == 0) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto collection_params_struct_status = GetStructVersionStatus(*collection_params);
    if (collection_params_struct_status != ASTL_STATUS_SUCCESS) {
      return collection_params_struct_status;
    }
    const auto collection_flags_status = ValidateCollectionParamsFlags(collection_params);
    if (collection_flags_status != ASTL_STATUS_SUCCESS) {
      return collection_flags_status;
    }

    SwitchSystemInfoToHostCapture();

    auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
    if (!orchestrator_or_error) {
      return orchestrator_or_error.error();
    }
    const auto& orchestrator = orchestrator_or_error->get();

    // get handles to internal components
    auto get_metric_manager_result = GetMetricManager();
    if (!get_metric_manager_result) {
      return get_metric_manager_result.error();
    }
    auto* metric_manager    = *get_metric_manager_result;
    auto  get_target_result = GetTargetFromHandle(target_handle);
    if (!get_target_result) {
      return get_target_result.error();
    }
    const auto*                                 target = *get_target_result;
    std::span<const astl_metric_group_handle_t> metric_group_handle_span{metric_group_handles, metric_group_count};
    auto                                        metric_handles_or_error =
        ExpandMetricGroupHandlesForTarget(metric_manager, target, metric_group_handle_span, true);
    if (!metric_handles_or_error) {
      return metric_handles_or_error.error();
    }
    auto& metric_handles_vector = *metric_handles_or_error;
    // collect on all the metrics we gathered from the given groups
    std::span<const astl_metric_handle_t> metric_handle_span{metric_handles_vector};
    return orchestrator->ConfigureMetricCollection(target, collection_params, metric_handle_span);
  }();
}

auto astlConfigureMetricGroupCollection(const astl_configure_metric_group_collection_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* collection_params    = params->collection_params;
  const auto* metric_group_handles = params->metric_group_handles;
  const auto  metric_group_count   = params->metric_group_count;
  if (!collection_params || !metric_group_handles || metric_group_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto collection_params_struct_status = GetStructVersionStatus(*collection_params);
  if (collection_params_struct_status != ASTL_STATUS_SUCCESS) {
    return collection_params_struct_status;
  }
  const auto collection_flags_status = ValidateCollectionParamsFlags(collection_params);
  if (collection_flags_status != ASTL_STATUS_SUCCESS) {
    return collection_flags_status;
  }
  SwitchSystemInfoToHostCapture();
  std::span<const astl_metric_group_handle_t> metric_group_handle_span{metric_group_handles, metric_group_count};
  return ConfigureMetricGroupCollectionOnAllTargets(collection_params, metric_group_handle_span);
}

auto astlReadImmediateOnTarget(const astl_read_immediate_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  const auto* target = *result;

  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return orchestrator->ReadImmediate(target);
}

auto astlReadImmediate(const astl_read_immediate_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator      = orchestrator_or_error->get();
  const auto& available_targets = orchestrator->GetTargets();
  for (const auto& target : available_targets) {
    // TODO(https://jira.arm.com/browse/ASTL-54)  -- dispatch read to the metric manager instead of the target
    auto result = orchestrator->ReadImmediate(target.get());
    if (result != ASTL_STATUS_SUCCESS) {
      return result;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto astlStartCollectionOnTarget(const astl_start_collection_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }

  const auto* target                = *result;
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return orchestrator->StartCollection(target);
}

auto astlStartCollection(const astl_start_collection_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return StartConfiguredTargets(*orchestrator, false);
}

auto astlStartCollectionOnTargetPaused(const astl_start_collection_on_target_paused_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }

  const auto* target                = *result;
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return orchestrator->StartCollectionPaused(target);
}

auto astlStartCollectionPaused(const astl_start_collection_paused_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return StartConfiguredTargets(*orchestrator, true);
}

auto astlPauseCollectionOnTarget(const astl_pause_collection_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto target_result = GetTargetFromHandle(target_handle);
  if (!target_result) {
    return target_result.error();
  }
  const auto* target                = *target_result;
  auto        orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator* orchestrator_ptr = orchestrator_or_error.value().get().get();
  astl_status_code    status           = orchestrator_ptr->PauseCollection(target);
  ASTL_LOG_DEBUG("PauseCollection on target '{}' returned with code: {}",
                 (target ? target->Name() : std::string{"<null>"}), status);
  return status;
}

// Helper: get the initialized orchestrator, iterate all targets, call op_fn on each, and return the
// first non-success status (or SUCCESS if all pass). Requires the orchestrator to already be initialized.
static auto ApplyToAllTargets(std::string_view operation_name, auto op_fn) noexcept -> astl_status_code {
  if (!astl::Orchestrator::IsInitialized()) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  auto orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator* orchestrator_ptr = orchestrator_or_error.value().get().get();
  astl_status_code    aggregate_status = ASTL_STATUS_SUCCESS;
  for (auto const& target_unique_ptr : orchestrator_ptr->GetTargets()) {
    auto status = op_fn(orchestrator_ptr, target_unique_ptr.get());
    if (status != ASTL_STATUS_SUCCESS && aggregate_status == ASTL_STATUS_SUCCESS) {
      aggregate_status = status;
    }
  }
  ASTL_LOG_DEBUG("{} returned with code: {}", operation_name, aggregate_status);
  return aggregate_status;
}

auto astlPauseCollection(const astl_pause_collection_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  return ApplyToAllTargets("PauseCollection", [](astl::Orchestrator* orchestrator, const astl::ITarget* target) {
    return orchestrator->PauseCollection(target);
  });
}

auto astlResumeCollectionOnTarget(const astl_resume_collection_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto target_result = GetTargetFromHandle(target_handle);
  if (!target_result) {
    return target_result.error();
  }
  const auto* target                = *target_result;
  auto        orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator* orchestrator_ptr = orchestrator_or_error.value().get().get();
  astl_status_code    status           = orchestrator_ptr->ResumeCollection(target);

  ASTL_LOG_DEBUG("ResumeCollection on target '{}' returned with code: {}",
                 (target ? target->Name() : std::string{"<null>"}), status);
  return status;
}

auto astlResumeCollection(const astl_resume_collection_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  return ApplyToAllTargets("ResumeCollection", [](astl::Orchestrator* orchestrator, const astl::ITarget* target) {
    return orchestrator->ResumeCollection(target);
  });
}

auto astlStopCollectionOnTarget(const astl_stop_collection_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }

  const auto* target                = *result;
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return orchestrator->StopCollection(target);
}

auto astlStopCollection(const astl_stop_collection_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  return ApplyToAllTargets("StopCollection", [](astl::Orchestrator* orchestrator, const astl::ITarget* target) {
    return orchestrator->StopCollection(target);
  });
}

/*** Save collection session to .astl file ***/
auto astlSaveCollection(const astl_save_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    if (!params) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto save_params_struct_status = GetStructVersionStatus(*params);
    if (save_params_struct_status != ASTL_STATUS_SUCCESS) {
      return save_params_struct_status;
    }
    if (params->flags != 0) {
      return ASTL_STATUS_INVALID_FLAG_VALUE;
    }

    if ((params->output_file_path == nullptr) || std::string_view{params->output_file_path}.empty()) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }

    // Ensure orchestrator is initialized.
    auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
    if (!orchestrator_or_error) {
      return orchestrator_or_error.error();
    }

    auto expanded_path = astl::ExpandFilePath(params->output_file_path);
    if (!expanded_path) {
      ASTL_LOG_ERROR("astlSaveCollection: invalid output_file_path '{}': {}",
                     (params->output_file_path ? params->output_file_path : "<null>"), expanded_path.error());
      return ASTL_STATUS_BAD_ARGUMENT;
    }

    return astl::Orchestrator::SaveToFile(*expanded_path);
  }();
}

/*** Load collection session from .astl file ***/
auto astlLoadCollection(const astl_load_params_t* params) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
    if (!params) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto load_params_struct_status = GetStructVersionStatus(*params);
    if (load_params_struct_status != ASTL_STATUS_SUCCESS) {
      return load_params_struct_status;
    }
    if (params->flags != 0) {
      return ASTL_STATUS_INVALID_FLAG_VALUE;
    }
    if ((params->input_file_path == nullptr) || std::string_view{params->input_file_path}.empty()) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }

    auto expanded_path = astl::ExpandFilePath(params->input_file_path);
    if (!expanded_path) {
      ASTL_LOG_ERROR("astlLoadCollection: invalid input_file_path '{}': {}",
                     (params->input_file_path ? params->input_file_path : "<null>"), expanded_path.error());
      return ASTL_STATUS_BAD_ARGUMENT;
    }

    // Reset the singleton so subsequent API calls rebuild the orchestrator from the provided file.
    astl::Orchestrator::ResetInstance();

    astl::ConfigurationManager::SetLoadFilePathOverride(std::optional<std::filesystem::path>{*expanded_path});

    // Force construction to validate the file can be loaded and extracted.
    auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
    if (!orchestrator_or_error) {
      return orchestrator_or_error.error();
    }

    (void)params->chunk_size_bytes;  // reserved for future streaming loader
    return ASTL_STATUS_SUCCESS;
  }();
}

/*** COLLECTED COUNTER SAMPLES ***/
auto astlGetCounterSampleCountOnTarget(const astl_get_counter_sample_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  return GetSampleCountOnTarget(params, "astlGetCounterSampleCountOnTarget", ParseCounterSampleCountRequest,
                                "orchestrator.GetProcessedMetricSamples");
}

auto astlGetCounterSamplesOnTarget(const astl_get_counter_samples_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  auto                        status = ValidateTimestampedApiParams(params, "astlGetCounterSamplesOnTarget");
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = ParseCounterSamplesRequest(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      status = PopulateCounterSamplesOutput(*request_or_error, params->start_ts, params->end_ts);
    }
  }

  return status;
}

/*** COLLECTED METRIC SAMPLES ***/
auto astlGetMetricSampleCountOnTarget(const astl_get_metric_sample_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  return GetSampleCountOnTarget(params, "astlGetMetricSampleCountOnTarget", ParseMetricSampleCountRequest,
                                "metric->GetProcessedSamples");
}

auto astlGetMetricSamplesOnTarget(const astl_get_metric_samples_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  auto                        status = ValidateApiParams(params);
  if (status == ASTL_STATUS_SUCCESS) {
    status = ValidateTimestampRange("astlGetMetricSamplesOnTarget", params->start_ts, params->end_ts);
  }

  MetricSamplesRequest request;
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = ParseMetricSamplesRequest(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      request = *request_or_error;
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    auto collected_samples_or_error = GetProcessedMetricSamples(request.metric, request.target);
    if (!collected_samples_or_error) {
      status = collected_samples_or_error.error();
    } else {
      std::vector<astl::ProcessedSampledData> filtered_storage;
      auto filtered_samples = std::span<const astl::ProcessedSampledData>(*collected_samples_or_error);
      if (params->start_ts != 0 || params->end_ts != 0) {
        filtered_storage = FilterSamplesInTimestampRange(*collected_samples_or_error, params->start_ts, params->end_ts);
        filtered_samples = filtered_storage;
      }

      status = ValidateMetricSamplesOutputCapacity(request, filtered_samples);
      if (status == ASTL_STATUS_SUCCESS) {
        CopyProcessedSamplesToOutput(filtered_samples, request.output_samples);
      }
    }
  }

  return status;
}

/***********************************************************************************
 **********************          METRIC SUMMARY API         ************************
 **********************************************************************************/

namespace {

// Runs MinMaxAvgSummarizer for the given samples and returns the summary, or an error status.
auto ComputeMinMaxStats(std::span<const astl::ProcessedSampledData> samples,
                        const astl_metric_props_t&                  metric_properties)
    -> std::expected<astl::MinMaxAvgSummary, astl_status_code> {
  astl::MinMaxAvgSummarizer summarizer;
  if (!summarizer.IsSupported(metric_properties.value_type, metric_properties.metric_type)) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Metric type not supported by MinMaxAvgSummarizer");
    return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
  }

  auto summary_result = summarizer.Summarize(samples);
  if (!summary_result) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Failed to compute summary");
    return std::unexpected(summary_result.error());
  }

  const auto* minmax = std::get_if<astl::MinMaxAvgSummary>(&(*summary_result));
  if (!minmax) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Unexpected summary type returned");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  return *minmax;
}

// Runs TimeWeightedAvgSummarizer for the given samples and returns the summary, or an error status.
// Pause markers for (target, metric) are retrieved from the orchestrator so
// that idle gaps between pause/resume cycles do not inflate sample weights.
auto ComputeTimeWeightedAvg(std::span<const astl::ProcessedSampledData> samples, const astl::ITarget* target)
    -> std::expected<astl::TimeWeightedAvgSummary, astl_status_code> {
  // Retrieve pause markers from the orchestrator.
  std::span<const astl::ProcessedSampleTimestamp> pause_markers_span;
  astl::PauseMarkersMap                           pause_markers_snapshot;
  auto                                            orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (orchestrator_or_error) {
    pause_markers_snapshot = orchestrator_or_error->get()->GetPauseMarkersSnapshot();
    auto target_it         = pause_markers_snapshot.find(target);
    if (target_it != pause_markers_snapshot.end()) {
      pause_markers_span = target_it->second;
    }
  }

  auto twa_result = astl::TimeWeightedAvgSummarizer::Summarize(samples, pause_markers_span);
  if (!twa_result.has_value()) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Failed to compute time-weighted average");
    return std::unexpected(twa_result.error());
  }
  const auto* twa_summary = std::get_if<astl::TimeWeightedAvgSummary>(&(*twa_result));
  if (!twa_summary) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Unexpected summary type returned for time-weighted average");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  return *twa_summary;
}

auto SelectRequestedAverageMode(astl_metric_statistics_t* summary) -> std::expected<uint32_t, astl_status_code> {
  const auto summary_struct_status = GetStructVersionStatus(*summary);
  if (summary_struct_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Invalid summary struct size: {} (expected {})", summary->size,
                   sizeof(astl_metric_statistics_t));
    return std::unexpected(summary_struct_status);
  }

  const uint32_t k_allowed_summary_flags =
      (ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG | ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG);
  const uint32_t request_flags = summary->flags;
  if ((request_flags & ~k_allowed_summary_flags) != 0U) {
    return std::unexpected(ASTL_STATUS_INVALID_FLAG_VALUE);
  }
  if ((request_flags & ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG) != 0U &&
      (request_flags & ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG) != 0U) {
    return std::unexpected(ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  const uint32_t selected_avg_mode = ((request_flags & ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG) != 0U)
                                         ? ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG
                                         : ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG;
  return selected_avg_mode;
}

auto InitializeMetricStatisticsSummary(astl_metric_statistics_t* summary, uint32_t selected_avg_mode) -> void {
  summary->flags = selected_avg_mode;
  summary->min   = {};
  summary->max   = {};
  summary->avg   = {};
  summary->count = 0;
}

struct MetricStatisticsInputs {
  const astl::ITarget*                        target{nullptr};
  std::span<const astl::ProcessedSampledData> samples;
  std::vector<astl::ProcessedSampledData>     filtered_samples_storage;
  astl_metric_props_t                         metric_properties{};
};

auto ResolveMetricStatisticsInputs(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                   uint64_t start_ts, uint64_t end_ts)
    -> std::expected<MetricStatisticsInputs, astl_status_code> {
  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return std::unexpected(get_target_result.error());
  }
  const auto* target = *get_target_result;

  auto get_metric_result = GetMetricFromHandle(metric_handle, target_handle);
  if (!get_metric_result) {
    return std::unexpected(get_metric_result.error());
  }
  const auto* metric = *get_metric_result;

  auto samples_result = GetProcessedMetricSamples(metric, target);
  if (!samples_result) {
    return std::unexpected(samples_result.error());
  }

  MetricStatisticsInputs inputs;
  inputs.target  = target;
  inputs.samples = *samples_result;
  if (start_ts != 0 || end_ts != 0) {
    inputs.filtered_samples_storage = FilterSamplesInTimestampRange(*samples_result, start_ts, end_ts);
    inputs.samples                  = inputs.filtered_samples_storage;
  }
  inputs.metric_properties.size = sizeof(astl_metric_props_t);
  auto props_status             = metric->GetProperties(&inputs.metric_properties);
  if (props_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Failed to get metric properties");
    return std::unexpected(props_status);
  }

  return inputs;
}

struct MetricStatisticsRequest {
  astl_target_handle_t      target_handle{nullptr};
  astl_metric_handle_t      metric_handle{nullptr};
  astl_metric_statistics_t* summary{nullptr};
  uint64_t                  start_ts{0};
  uint64_t                  end_ts{0};
};

auto ParseMetricStatisticsRequest(const astl_get_metric_statistics_on_target_params_t& params)
    -> std::expected<MetricStatisticsRequest, astl_status_code> {
  if (!params.target_handle || !params.metric_handle || !params.summary) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Invalid argument(s)");
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  return MetricStatisticsRequest{
      .target_handle = params.target_handle,
      .metric_handle = params.metric_handle,
      .summary       = params.summary,
      .start_ts      = params.start_ts,
      .end_ts        = params.end_ts,
  };
}

auto PopulateMetricStatisticsSummary(astl_metric_statistics_t* summary, const MetricStatisticsInputs& inputs,
                                     bool is_twa) -> astl_status_code {
  auto minmax_result = ComputeMinMaxStats(inputs.samples, inputs.metric_properties);
  if (!minmax_result) {
    return minmax_result.error();
  }

  summary->count = minmax_result->count;
  if (minmax_result->min.has_value()) {
    summary->min = minmax_result->min->ToAstlUnionValue().first;
  }
  if (minmax_result->max.has_value()) {
    summary->max = minmax_result->max->ToAstlUnionValue().first;
  }
  if (!is_twa && minmax_result->avg.has_value()) {
    summary->avg = minmax_result->avg->ToAstlUnionValue().first;
  }
  if (!is_twa) {
    return ASTL_STATUS_SUCCESS;
  }

  auto twa_result = ComputeTimeWeightedAvg(inputs.samples, inputs.target);
  if (!twa_result) {
    return twa_result.error();
  }
  if (twa_result->time_weighted_avg.has_value()) {
    summary->avg = twa_result->time_weighted_avg->ToAstlUnionValue().first;
  }
  return ASTL_STATUS_SUCCESS;
}

}  // namespace

auto astlGetMetricStatisticsOnTarget(const astl_get_metric_statistics_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  auto                        status = ValidateApiParams(params);

  MetricStatisticsRequest request;
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = ParseMetricStatisticsRequest(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      request = *request_or_error;
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    status = ValidateTimestampRange("astlGetMetricStatisticsOnTarget", request.start_ts, request.end_ts);
  }

  uint32_t selected_avg_mode = ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG;
  if (status == ASTL_STATUS_SUCCESS) {
    auto selected_avg_mode_or_error = SelectRequestedAverageMode(request.summary);
    if (!selected_avg_mode_or_error) {
      status = selected_avg_mode_or_error.error();
    } else {
      selected_avg_mode = *selected_avg_mode_or_error;
      InitializeMetricStatisticsSummary(request.summary, selected_avg_mode);
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    const bool is_twa = (selected_avg_mode == ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG);
    auto       inputs_or_error =
        ResolveMetricStatisticsInputs(request.target_handle, request.metric_handle, request.start_ts, request.end_ts);
    if (!inputs_or_error) {
      status = inputs_or_error.error();
    } else {
      status = PopulateMetricStatisticsSummary(request.summary, *inputs_or_error, is_twa);
    }
  }

  return status;
}

namespace {

/**
 * @brief Shared helper: run the HistogramSummarizer (discrete mode) for the given
 *        target/metric and return the HistogramSummary, or an error status.
 */
auto ComputeDiscreteHistogram(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle, uint64_t start_ts,
                              uint64_t end_ts) -> std::expected<astl::HistogramSummary, astl_status_code> {
  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return std::unexpected(get_target_result.error());
  }
  const auto* target = *get_target_result;

  auto get_metric_result = GetMetricFromHandle(metric_handle, target_handle);
  if (!get_metric_result) {
    return std::unexpected(get_metric_result.error());
  }
  const auto* metric = *get_metric_result;

  // Check supported metric/value type
  astl_metric_props_t metric_properties{};
  metric_properties.size = sizeof(astl_metric_props_t);
  auto props_status      = metric->GetProperties(&metric_properties);
  if (props_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("ComputeDiscreteHistogram: Failed to get metric properties");
    return std::unexpected(props_status);
  }

  astl::HistogramSummarizer summarizer;  // default constructor = discrete mode
  if (!summarizer.IsSupported(metric_properties.value_type, metric_properties.metric_type)) {
    ASTL_LOG_ERROR("ComputeDiscreteHistogram: Metric type not supported by HistogramSummarizer");
    return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
  }

  auto samples_result = GetProcessedMetricSamples(metric, target);
  if (!samples_result) {
    return std::unexpected(samples_result.error());
  }

  std::vector<astl::ProcessedSampledData> filtered_samples_storage;
  auto filtered_samples = std::span<const astl::ProcessedSampledData>(*samples_result);
  if (start_ts != 0 || end_ts != 0) {
    filtered_samples_storage = FilterSamplesInTimestampRange(*samples_result, start_ts, end_ts);
    filtered_samples         = filtered_samples_storage;
  }

  auto summary_result = summarizer.Summarize(filtered_samples);
  if (!summary_result) {
    ASTL_LOG_ERROR("ComputeDiscreteHistogram: Failed to compute histogram");
    return std::unexpected(summary_result.error());
  }

  auto* histogram = std::get_if<astl::HistogramSummary>(&(*summary_result));
  if (!histogram) {
    ASTL_LOG_ERROR("ComputeDiscreteHistogram: Unexpected summary type returned");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  return std::move(*histogram);
}

}  // namespace

auto astlGetMetricDiscreteHistogramBinCountOnTarget(
    const astl_get_metric_discrete_histogram_bin_count_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto timestamp_status =
      ValidateTimestampRange("astlGetMetricDiscreteHistogramBinCountOnTarget", params->start_ts, params->end_ts);
  if (timestamp_status != ASTL_STATUS_SUCCESS) {
    return timestamp_status;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       bin_count     = params->bin_count;
  if (!target_handle || !metric_handle || !bin_count) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramBinCountOnTarget: Invalid argument(s)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto histogram_result = ComputeDiscreteHistogram(target_handle, metric_handle, params->start_ts, params->end_ts);
  if (!histogram_result) {
    return histogram_result.error();
  }

  if (histogram_result->bins.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramBinCountOnTarget: bin count exceeds uint32_t max: {}",
                   histogram_result->bins.size());
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  *bin_count = static_cast<uint32_t>(histogram_result->bins.size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetMetricDiscreteHistogramOnTarget(
    const astl_get_metric_discrete_histogram_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto timestamp_status =
      ValidateTimestampRange("astlGetMetricDiscreteHistogramOnTarget", params->start_ts, params->end_ts);
  if (timestamp_status != ASTL_STATUS_SUCCESS) {
    return timestamp_status;
  }

  auto request_or_error = ParseMetricHistogramOutputRequest(*params);
  if (!request_or_error) {
    return request_or_error.error();
  }

  auto histogram_result = ComputeDiscreteHistogram(request_or_error->target_handle, request_or_error->metric_handle,
                                                   params->start_ts, params->end_ts);
  if (!histogram_result) {
    return histogram_result.error();
  }
  return PopulateDiscreteHistogramBins(*histogram_result, request_or_error->bins, request_or_error->out_bin_count);
}

/***********************************************************************************
 **********************     POST-COLLECTION PROCESSING      ************************
 **********************************************************************************/

namespace {

/// Validate the caller-supplied windows array shared by both crop APIs.
auto ValidateCropWindows(const astl_crop_window_t* windows, uint32_t window_count) noexcept -> astl_status_code {
  return [&]() noexcept -> astl_status_code {
    if (windows == nullptr) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    if (window_count == 0) {
      ASTL_LOG_ERROR("astlCropSamples: window_count must be >= 1");
      return ASTL_STATUS_BAD_ARGUMENT;
    }

    std::span<const astl_crop_window_t> windows_span{windows, window_count};
    const auto                          crop_window_struct_status = GetStructVersionStatus(windows_span.front());
    if (crop_window_struct_status != ASTL_STATUS_SUCCESS) {
      return crop_window_struct_status;
    }

    uint32_t window_index = 0;
    for (const auto& window : windows_span) {
      if (window.flags != 0U) {
        ASTL_LOG_ERROR("astlCropSamples: windows[{}].flags must be 0", window_index);
        return ASTL_STATUS_INVALID_FLAG_VALUE;
      }
      if (window.start_ts != 0 && window.end_ts != 0 && window.start_ts > window.end_ts) {
        ASTL_LOG_ERROR("astlCropSamples: windows[{}] has start_ts ({}) > end_ts ({})", window_index, window.start_ts,
                       window.end_ts);
        return ASTL_STATUS_BAD_ARGUMENT;
      }
      ++window_index;
    }
    return ASTL_STATUS_SUCCESS;
  }();
}

struct CropSamplesOnTargetRequest {
  const astl::ITarget*                target{nullptr};
  std::span<const astl_crop_window_t> windows;
};

struct CropMetricSamplesOnTargetRequest {
  const astl::ITarget*                target{nullptr};
  const astl::IMetric*                metric{nullptr};
  std::span<const astl_crop_window_t> windows;
};

struct CropSamplesRequest {
  std::span<const astl_crop_window_t> windows;
};

auto ParseCropSamplesRequest(const astl_crop_samples_params_t& params)
    -> std::expected<CropSamplesRequest, astl_status_code> {
  const auto windows_status = ValidateCropWindows(params.windows, params.window_count);
  if (windows_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(windows_status);
  }

  return CropSamplesRequest{
      .windows = std::span<const astl_crop_window_t>{params.windows, params.window_count},
  };
}

auto ParseCropSamplesOnTargetRequest(const astl_crop_samples_on_target_params_t& params)
    -> std::expected<CropSamplesOnTargetRequest, astl_status_code> {
  if (!params.target_handle) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  const auto windows_status = ValidateCropWindows(params.windows, params.window_count);
  if (windows_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(windows_status);
  }

  auto target_or_error = GetTargetFromHandle(params.target_handle);
  if (!target_or_error) {
    return std::unexpected(target_or_error.error());
  }

  return CropSamplesOnTargetRequest{
      .target  = *target_or_error,
      .windows = std::span<const astl_crop_window_t>{params.windows, params.window_count},
  };
}

auto ParseCropMetricSamplesOnTargetRequest(const astl_crop_metric_samples_on_target_params_t& params)
    -> std::expected<CropMetricSamplesOnTargetRequest, astl_status_code> {
  if (!params.target_handle || !params.metric_handle) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  const auto windows_status = ValidateCropWindows(params.windows, params.window_count);
  if (windows_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(windows_status);
  }

  auto resolved_components = ResolveTargetAndMetricManager(params.target_handle);
  if (!resolved_components) {
    return std::unexpected(resolved_components.error());
  }

  auto metric_or_error =
      resolved_components->metric_manager->GetMetricOnTarget(params.metric_handle, resolved_components->target);
  if (!metric_or_error) {
    return std::unexpected(metric_or_error.error());
  }

  return CropMetricSamplesOnTargetRequest{
      .target  = resolved_components->target,
      .metric  = *metric_or_error,
      .windows = std::span<const astl_crop_window_t>{params.windows, params.window_count},
  };
}

auto ValidateAllCropTargetsStopped(const astl::Orchestrator& orchestrator) -> astl_status_code {
  const auto all_states = orchestrator.GetAllTargetCollectionStates();
  for (const auto& [target_ptr, state] : all_states) {
    if (state == astl::Orchestrator::TargetCollectionState::STARTED ||
        state == astl::Orchestrator::TargetCollectionState::PAUSED) {
      return ASTL_STATUS_COLLECTION_NOT_STOPPED;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto CropSamplesOnAllTargets(astl::Orchestrator& orchestrator, std::span<const astl_crop_window_t> windows)
    -> astl_status_code {
  astl_status_code aggregate_status = ASTL_STATUS_SUCCESS;
  for (const auto& target_unique_ptr : orchestrator.GetTargets()) {
    const auto status = orchestrator.CropSamplesOnTarget(target_unique_ptr.get(), windows);
    if (status != ASTL_STATUS_SUCCESS && aggregate_status == ASTL_STATUS_SUCCESS) {
      aggregate_status = status;
    }
  }
  return aggregate_status;
}

}  // namespace

auto astlCropSamplesOnTarget(const astl_crop_samples_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  auto                        status = ValidateApiParams(params);

  CropSamplesOnTargetRequest request;
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = ParseCropSamplesOnTargetRequest(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      request = *request_or_error;
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    auto orchestrator_or_error = GetOrchestratorInstance();
    if (!orchestrator_or_error) {
      status = orchestrator_or_error.error();
    } else {
      status = (*orchestrator_or_error)->CropSamplesOnTarget(request.target, request.windows);
    }
  }

  return status;
}

auto astlCropMetricSamplesOnTarget(const astl_crop_metric_samples_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  auto                        status = ValidateApiParams(params);

  CropMetricSamplesOnTargetRequest request;
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = ParseCropMetricSamplesOnTargetRequest(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      request = *request_or_error;
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    auto orchestrator_or_error = GetOrchestratorInstance();
    if (!orchestrator_or_error) {
      status = orchestrator_or_error.error();
    } else {
      status = (*orchestrator_or_error)->CropMetricSamplesOnTarget(request.target, request.metric, request.windows);
    }
  }

  return status;
}

auto astlCropSamples(const astl_crop_samples_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  auto                        status = ValidateApiParams(params);

  CropSamplesRequest request;
  if (status == ASTL_STATUS_SUCCESS) {
    auto request_or_error = ParseCropSamplesRequest(*params);
    if (!request_or_error) {
      status = request_or_error.error();
    } else {
      request = *request_or_error;
    }
  }

  astl::Orchestrator* orchestrator = nullptr;
  if (status == ASTL_STATUS_SUCCESS) {
    auto orchestrator_or_error = GetOrchestratorInstance();
    if (!orchestrator_or_error) {
      status = orchestrator_or_error.error();
    } else {
      orchestrator = *orchestrator_or_error;
    }
  }

  if (status == ASTL_STATUS_SUCCESS) {
    status = ValidateAllCropTargetsStopped(*orchestrator);
  }

  if (status == ASTL_STATUS_SUCCESS) {
    status = CropSamplesOnAllTargets(*orchestrator, request.windows);
  }

  return status;
}
