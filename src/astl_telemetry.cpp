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

auto GetOutputManager() noexcept -> std::expected<astl::IOutputManager*, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator   = orchestrator_or_error->get();
  const auto& output_manager = orchestrator->GetOutputManager();
  if (!output_manager) {
    ASTL_LOG_ERROR("No output manager assigned to orchestrator");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return output_manager.get();
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
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
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

auto GetProcessedSamplesSnapshot() noexcept -> std::expected<astl::ProcessedSamplesMap, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator = orchestrator_or_error->get();
  // Snapshot return avoids exposing references to mutable shared state across threads.
  return orchestrator->GetProcessedSamplesSnapshot();
}

constexpr uint32_t kFirstElementIdx{0};

template <typename ParamsT>
auto ValidateApiParams(const ParamsT* params) noexcept -> astl_status_code {
  if (params == nullptr) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (params->size != sizeof(ParamsT)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }
  if (params->flags != 0U) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ValidateCollectionParamsFlags(const astl_collection_params_t* collection_params) noexcept -> astl_status_code {
  const uint32_t k_allowed_collection_flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD |
                                              ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY |
                                              ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_INTERFERENCE;
  const uint32_t request_flags = collection_params->flags;
  if ((request_flags & ~k_allowed_collection_flags) != 0U) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  // Optimization flags are mutually exclusive.
  if (request_flags != 0U && ((request_flags & (request_flags - 1U)) != 0U)) {
    return ASTL_STATUS_BAD_ARGUMENT;
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
  if (metric_buffer[0].size < sizeof(astl_metric_props_t)) {
    return ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION;
  }
  if (metric_buffer[0].size > sizeof(astl_metric_props_t)) {
    return ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION;
  }

  for (size_t metric_idx = 0; metric_idx < metric_handles.size(); ++metric_idx) {
    auto result = metric_manager->GetProperties(metric_handles[metric_idx], &metric_buffer[metric_idx]);
    if (result != ASTL_STATUS_SUCCESS) {
      return result;
    }
  }

  return ASTL_STATUS_SUCCESS;
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
  auto* system_info = params->system_info;
  if (!system_info) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  if (system_info->size != sizeof(astl_platform_props_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }

  const uint32_t k_allowed_source_flags = (ASTL_SYSTEM_INFO_FLAG_HOST | ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION);
  const uint32_t request_flags          = system_info->flags;
  if ((request_flags & ~k_allowed_source_flags) != 0U) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if ((request_flags & ASTL_SYSTEM_INFO_FLAG_HOST) != 0U &&
      (request_flags & ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION) != 0U) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  const astl::PlatformInfoData*                 info = nullptr;
  std::shared_ptr<const astl::PlatformInfoData> loaded_info_ref{};
  uint32_t                                      selected_flag = 0U;
  if ((request_flags & ASTL_SYSTEM_INFO_FLAG_HOST) != 0U) {
    info          = &astl::GetHostPlatformInfo();
    selected_flag = ASTL_SYSTEM_INFO_FLAG_HOST;
  } else if ((request_flags & ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION) != 0U) {
    loaded_info_ref = astl::GetLoadedPlatformInfo();
    info            = loaded_info_ref.get();
    if (info == nullptr) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    selected_flag = ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
  } else {
    loaded_info_ref = astl::GetLoadedPlatformInfo();
    if (loaded_info_ref != nullptr) {
      info          = loaded_info_ref.get();
      selected_flag = ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
    } else {
      info          = &astl::GetHostPlatformInfo();
      selected_flag = ASTL_SYSTEM_INFO_FLAG_HOST;
    }
  }

  system_info->flags            = selected_flag;
  system_info->soc_name         = info->soc_name.empty() ? nullptr : info->soc_name.c_str();
  system_info->vendor_id        = info->vendor_id.empty() ? nullptr : info->vendor_id.c_str();
  system_info->os_name          = info->os_name.empty() ? nullptr : info->os_name.c_str();
  system_info->kernel_name      = info->kernel_name.empty() ? nullptr : info->kernel_name.c_str();
  system_info->kernel_version   = info->kernel_version.empty() ? nullptr : info->kernel_version.c_str();
  system_info->kernel_release   = info->kernel_release.empty() ? nullptr : info->kernel_release.c_str();
  system_info->firmware_version = info->firmware_version.empty() ? nullptr : info->firmware_version.c_str();
  system_info->hostname         = info->hostname.empty() ? nullptr : info->hostname.c_str();
  system_info->architecture     = info->architecture.empty() ? nullptr : info->architecture.c_str();

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
    return ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL;
  }
  *target_count = static_cast<uint32_t>(targets_size);
  return ASTL_STATUS_SUCCESS;
}

using VersionedPropertiesSpan = std::variant<std::span<astl_target_props_t> >;

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
      if (sizeof(astl_target_props_t) > *given_struct_size) {
        return std::unexpected(ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION);
      } else {
        return std::unexpected(ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION);
      }
  }
}

auto astlGetTargets(const astl_get_targets_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto* targets      = params->targets;
  auto* target_count = params->target_count;
  if (!targets) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!target_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (*target_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();

  auto const& available_targets       = orchestrator->GetTargets();
  auto        available_targets_count = available_targets.size();
  if (available_targets_count == 0) {
    *target_count = 0;
    return ASTL_STATUS_NO_TARGETS_FOUND;
  }
  // note- if the allocated buffer is _larger_ than the number of targets we have available,
  // we only write the targets we have available and communicate that number through target_count
  if (available_targets_count > *target_count) {
    return ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL;
  }
  // convert our raw pointer to _some_ version of astl_target_props_t into a std::span
  // of a specific struct, maybe astl_target_props_t, maybe astl_target_properties_v0_t,
  // or just an error if we can't support the given struct size.
  auto target_span = GetVersionedTargetPropertiesSpan(targets, *target_count);
  if (!target_span) {
    return target_span.error();
  }

  // lambda for std::visit to run on the span of variant-versioned targets
  const auto get_props_fn = [&available_targets, target_count](auto&& target_properties) {
    // we have some span of some version of targets here - so dispatch to the overloaded GetProperties
    // that supports that version
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
    return std::visit(get_props_fn, *target_span);
  } catch (const std::bad_variant_access& e) {
    ASTL_LOG_ERROR("astlGetTargets: bad_variant_access exception: {}", e.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
}

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/
// TODO(ASTL-180) counter should be re-implemented as just a RawMetric.
auto astlGetCounterCount(const astl_get_counter_count_params_t* params) noexcept -> astl_status_code {
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
    return ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL;
  }
  *counter_count = static_cast<uint32_t>(num_counters);
  return ASTL_STATUS_SUCCESS;
}

auto astlGetCounters(const astl_get_counters_params_t* params) noexcept -> astl_status_code {
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
  const auto                      counter_buffer_size  = *counter_count;
  auto                            counters_buffer_size = *counter_count;
  std::span<astl_counter_props_t> output_counters{counters, *counter_count};
  *counter_count = 0;  // in case there's an error to return

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  const auto& available_counters_result = metric_manager->GetAvailableCounters(target);
  if (!available_counters_result) {
    return available_counters_result.error();
  }
  const auto& available_counters = *available_counters_result;

  if (available_counters.size() > counters_buffer_size) {
    *counter_count = 0;
    return ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL;
  }
  if (available_counters.empty()) {
    *counter_count = 0;
    return ASTL_STATUS_NO_COUNTERS_FOUND;
  }
  std::span<astl_counter_props_t> counter_span{counters, counter_buffer_size};
  auto                            counter_struct_size = GetFirstElementSizeField(counter_span);
  if (!counter_struct_size) {
    return counter_struct_size.error();
  }
  if (*counter_struct_size < sizeof(astl_counter_props_t)) {
    return ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION;
  }
  if (*counter_struct_size > sizeof(astl_counter_props_t)) {
    return ASTL_STATUS_NEW_COUNTER_PROPERTIES_STRUCT_VERSION;
  }
  for (size_t i = 0; i < available_counters.size(); ++i) {
    auto result = metric_manager->GetCounterProperties(available_counters[i], &output_counters[i]);
    if (result != ASTL_STATUS_SUCCESS) {
      // indicate how many we successfully filled in (not the failing one)
      *counter_count = static_cast<uint32_t>(i);
      return result;
    }
  }
  auto result =
      available_counters.size() == counters_buffer_size ? ASTL_STATUS_SUCCESS : ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED;
  *counter_count = static_cast<uint32_t>(available_counters.size());
  return result;
}

/***********************************************************************************
 **********************              METRIC                    *********************
 **********************************************************************************/

auto astlGetMetricCount(const astl_get_metric_count_params_t* params) noexcept -> astl_status_code {
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
}

auto astlGetMetrics(const astl_get_metrics_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle = params->target_handle;
  auto*       metrics       = params->metrics;
  auto*       metric_count  = params->metric_count;
  // check input arguments
  if (!target_handle || !metrics || !metric_count || *metric_count == 0) {
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

  std::span<astl_metric_props_t> output_metrics{metrics, *metric_count};
  *metric_count = 0;  // in case there's an error to return

  const auto& available_metrics_result = metric_manager->GetAvailableMetrics(target);
  if (!available_metrics_result) {
    return available_metrics_result.error();
  }
  const auto& available_metrics = *available_metrics_result;
  if (available_metrics.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("metric_manager->GetMetrics reports absurdly large number of metrics: {}", available_metrics.size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (available_metrics.size() > output_metrics.size()) {
    return ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL;
  }
  if (available_metrics.empty()) {
    return ASTL_STATUS_NO_METRICS_FOUND;
  }
  // check struct size of astl_metric_props_t
  auto metric_struct_size = GetFirstElementSizeField(output_metrics);
  if (!metric_struct_size) {
    return metric_struct_size.error();
  }
  if (*metric_struct_size < sizeof(astl_metric_props_t)) {
    return ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION;
  }
  if (*metric_struct_size > sizeof(astl_metric_props_t)) {
    return ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION;
  }
  // copy properties from metrics to the provided buffer
  for (size_t i = 0; i < available_metrics.size(); ++i) {
    auto result = metric_manager->GetProperties(available_metrics[i], &output_metrics[i]);
    if (result != ASTL_STATUS_SUCCESS) {
      // on failure, indicate how many we successfully filled in (not the failing one)
      *metric_count = static_cast<uint32_t>(i);
      return result;
    }
  }
  auto result =
      output_metrics.size() > available_metrics.size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED : ASTL_STATUS_SUCCESS;
  *metric_count = static_cast<uint32_t>(available_metrics.size());
  return result;
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
    *state_count = 0;
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
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
    *state_count = 0;
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
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
  if (*state_struct_size != sizeof(astl_state_props_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
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
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle      = params->target_handle;
  auto*       metric_groups      = params->metric_groups;
  auto*       metric_group_count = params->metric_group_count;
  if (!target_handle || !metric_groups || !metric_group_count || *metric_group_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto metric_groups_properties = std::span<astl_metric_group_props_t>{metric_groups, *metric_group_count};
  *metric_group_count           = 0;
  if (metric_groups_properties[0].size < sizeof(astl_metric_group_props_t)) {
    return ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION;
  }
  if (metric_groups_properties[0].size > sizeof(astl_metric_group_props_t)) {
    return ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION;
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

  auto groups_result = metric_manager->GetMetricGroups(target);
  if (!groups_result) {
    return groups_result.error();
  }
  if (metric_groups_properties.size() < groups_result->size()) {
    return ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL;
  }
  for (size_t i = 0; i < std::min(groups_result->size(), metric_groups_properties.size()); ++i) {
    auto result = PopulateMetricGroupProperties(metric_manager, (*groups_result)[i], metric_groups_properties[i]);
    if (result != ASTL_STATUS_SUCCESS) {
      *metric_group_count = static_cast<uint32_t>(i);
      return result;
    }
  }
  auto result         = metric_groups_properties.size() > groups_result->size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
                                                                                : ASTL_STATUS_SUCCESS;
  *metric_group_count = static_cast<uint32_t>(groups_result->size());
  return result;
}

auto astlGetMetricGroups(const astl_get_metric_groups_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  auto* metric_groups      = params->metric_groups;
  auto* metric_group_count = params->metric_group_count;
  if (!metric_groups || !metric_group_count || *metric_group_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto metric_groups_properties = std::span<astl_metric_group_props_t>{metric_groups, *metric_group_count};
  *metric_group_count           = 0;
  if (metric_groups_properties[0].size < sizeof(astl_metric_group_props_t)) {
    return ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION;
  }
  if (metric_groups_properties[0].size > sizeof(astl_metric_group_props_t)) {
    return ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION;
  }
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto*      metric_manager = *get_metric_manager_result;
  const auto groups_result  = metric_manager->GetMetricGroups();
  if (metric_groups_properties.size() < groups_result.size()) {
    return ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL;
  }
  for (size_t i = 0; i < std::min(groups_result.size(), metric_groups_properties.size()); ++i) {
    auto result = PopulateMetricGroupProperties(metric_manager, groups_result[i], metric_groups_properties[i]);
    if (result != ASTL_STATUS_SUCCESS) {
      *metric_group_count = static_cast<uint32_t>(i);
      return result;
    }
  }
  auto result         = metric_groups_properties.size() > groups_result.size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
                                                                               : ASTL_STATUS_SUCCESS;
  *metric_group_count = static_cast<uint32_t>(groups_result.size());
  return result;
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
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* const metric_group_handle = params->metric_group_handle;
  auto*             metrics             = params->metrics;
  auto*             metric_count        = params->metric_count;
  if (!metric_group_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metric_group_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_count) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metric_count ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metrics) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metrics ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;

  ASTL_LOG_TRACE("astlGetMetricGroupMetrics: getting metrics in group");
  auto metrics_in_group = metric_manager->GetMetricsInGroup(metric_group_handle);
  if (!metrics_in_group) {
    return metrics_in_group.error();
  }
  if (metrics_in_group->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("GetMetricsInGroup reports absurdly large number of metrics: {}", metrics_in_group->size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  const auto required_metric_count = static_cast<uint32_t>(metrics_in_group->size());
  if (*metric_count == 0) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metric_count cannot be 0 when metrics ptr is non-null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::span<astl_metric_props_t> metrics_properties{metrics, *metric_count};
  if (metrics_properties.size() < metrics_in_group->size()) {
    *metric_count = required_metric_count;
    return ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL;
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
    return ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL;
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

/*** CONFIGURE COUNTERS ***/
auto astlConfigureCounterCollectionOnTarget(const astl_configure_counter_collection_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  const auto* target_handle     = params->target_handle;
  const auto* collection_params = params->collection_params;
  const auto* counter_handles   = params->counter_handles;
  const auto  counter_count     = params->counter_count;
  if (!target_handle || !collection_params || !counter_handles || counter_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  SwitchSystemInfoToHostCapture();

  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  const auto* target                    = *result;
  auto        get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto* metric_manager = *get_metric_manager_result;
  // get the number of counters
  const auto& available_counters = metric_manager->GetAvailableCounters(target);
  if (!available_counters) {
    return available_counters.error();
  }
  const auto num_counters = available_counters->size();

  if (counter_count > num_counters) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (sizeof(astl_collection_params_t) < collection_params->size) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (sizeof(astl_collection_params_t) > collection_params->size) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  const auto collection_flags_status = ValidateCollectionParamsFlags(collection_params);
  if (collection_flags_status != ASTL_STATUS_SUCCESS) {
    return collection_flags_status;
  }
  std::span<const astl_counter_handle_t> counter_handle_span{counter_handles, counter_count};
  auto const&                            orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();

  return orchestrator->ConfigureCounterCollection(target, collection_params, counter_handle_span);
}

auto astlConfigureCounterCollection(const astl_configure_counter_collection_params_t* params) noexcept
    -> astl_status_code {
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
  if (sizeof(astl_collection_params_t) < collection_params->size) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (sizeof(astl_collection_params_t) > collection_params->size) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
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
}

/*** CONFIGURE METRICS ***/
auto astlConfigureMetricCollectionOnTarget(const astl_configure_metric_collection_on_target_params_t* params) noexcept
    -> astl_status_code {
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
  if (collection_params->size < sizeof(astl_collection_params_t)) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (collection_params->size > sizeof(astl_collection_params_t)) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
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
}

auto astlConfigureMetricCollection(const astl_configure_metric_collection_params_t* params) noexcept
    -> astl_status_code {
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
  if (sizeof(astl_collection_params_t) < collection_params->size) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (sizeof(astl_collection_params_t) > collection_params->size) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
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
}

/*** CONFIGURE METRIC GROUPS ***/
auto astlConfigureMetricGroupCollectionOnTarget(
    const astl_configure_metric_group_collection_on_target_params_t* params) noexcept -> astl_status_code {
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
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!collection_params) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_group_handles) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (metric_group_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (collection_params->size < sizeof(astl_collection_params_t)) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (collection_params->size > sizeof(astl_collection_params_t)) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
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
  if (collection_params->size < sizeof(astl_collection_params_t)) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (collection_params->size > sizeof(astl_collection_params_t)) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
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
  const auto& targets      = orchestrator->GetTargets();
  if (targets.empty()) {
    return ASTL_STATUS_NO_TARGETS_FOUND;
  }

  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto*                                       metric_manager = *get_metric_manager_result;
  std::span<const astl_metric_group_handle_t> metric_group_handle_span{metric_group_handles, metric_group_count};

  bool any_target_configured = false;
  for (const auto& target_ptr : targets) {
    const auto* target = target_ptr.get();
    auto        metric_handles_or_error =
        ExpandMetricGroupHandlesForTarget(metric_manager, target, metric_group_handle_span, false);
    if (!metric_handles_or_error) {
      return metric_handles_or_error.error();
    }
    if (metric_handles_or_error->empty()) {
      continue;
    }

    std::span<const astl_metric_handle_t> metric_handle_span{*metric_handles_or_error};
    auto status = orchestrator->ConfigureMetricCollection(target, collection_params, metric_handle_span);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
    any_target_configured = true;
  }

  return any_target_configured ? ASTL_STATUS_SUCCESS : ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET;
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
  // TODO(ASTL-250,ASTL-326): Remove ASTL_STATUS_NOT_IMPLEMENTED once pause/resume is implemented in collector and
  // metric managers
  status = ASTL_STATUS_NOT_IMPLEMENTED;

  return status;
}

auto astlPauseCollection(const astl_pause_collection_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  // Do not trigger lazy construction; require `Orchestrator::GetInstance()` to have run.
  if (!astl::Orchestrator::IsInitialized()) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
  auto orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator* orchestrator_ptr = orchestrator_or_error.value().get().get();
  astl_status_code    aggregate_status = ASTL_STATUS_SUCCESS;
  for (auto const& target_unique_ptr : orchestrator_ptr->GetTargets()) {
    auto status = orchestrator_ptr->PauseCollection(target_unique_ptr.get());
    if (status != ASTL_STATUS_SUCCESS && aggregate_status == ASTL_STATUS_SUCCESS) {
      aggregate_status = status;
    }
  }
  ASTL_LOG_DEBUG("PauseCollection returned with code: {}", aggregate_status);
  // TODO(ASTL-250,ASTL-326): Remove ASTL_STATUS_NOT_IMPLEMENTED once pause/resume is implemented in collector and
  // metric managers
  aggregate_status = ASTL_STATUS_NOT_IMPLEMENTED;
  return aggregate_status;
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
  // TODO(ASTL-250,ASTL-326): Remove ASTL_STATUS_NOT_IMPLEMENTED once pause/resume is implemented in collector and
  // metric managers
  status = ASTL_STATUS_NOT_IMPLEMENTED;

  return status;
}

auto astlResumeCollection(const astl_resume_collection_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (!astl::Orchestrator::IsInitialized()) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
  auto orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator* orchestrator_ptr = orchestrator_or_error.value().get().get();
  astl_status_code    aggregate_status = ASTL_STATUS_SUCCESS;
  for (auto const& target_unique_ptr : orchestrator_ptr->GetTargets()) {
    auto status = orchestrator_ptr->ResumeCollection(target_unique_ptr.get());
    if (status != ASTL_STATUS_SUCCESS && aggregate_status == ASTL_STATUS_SUCCESS) {
      aggregate_status = status;
    }
  }
  ASTL_LOG_DEBUG("ResumeCollection returned with code: {}", aggregate_status);
  // TODO(ASTL-250,ASTL-326): Remove ASTL_STATUS_NOT_IMPLEMENTED once pause/resume is implemented in collector and
  // metric managers
  aggregate_status = ASTL_STATUS_NOT_IMPLEMENTED;
  return aggregate_status;
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
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/*** Save collection session to .astl file ***/
auto astlSaveCollection(const astl_save_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  if (!params) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (params->size != sizeof(astl_save_params_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }
  if (params->flags != 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
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
}

/*** Load collection session from .astl file ***/
auto astlLoadCollection(const astl_load_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  if (!params) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (params->size != sizeof(astl_load_params_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }
  if (params->flags != 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
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
}

/*** COLLECTED COUNTER SAMPLES ***/
auto astlGetCounterSampleCountOnTarget(const astl_get_counter_sample_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle  = params->target_handle;
  const auto* counter_handle = params->counter_handle;
  auto*       sample_count   = params->sample_count;
  if (!target_handle || !counter_handle || !sample_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  const auto* target = *result;

  auto get_counter_result = GetCounterFromHandle(counter_handle, target);
  if (!get_counter_result) {
    return get_counter_result.error();
  }
  const auto* counter               = *get_counter_result;
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator& orchestrator  = *(orchestrator_or_error.value().get());
  auto                sample_result = orchestrator.GetProcessedMetricSamples(counter, target);
  if (!sample_result) {
    return sample_result.error();
  }
  if (sample_result->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("orchestrator.GetProcessedMetricSamples reports absurdly large number of samples: {}",
                   sample_result->size());
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }
  *sample_count = static_cast<uint32_t>(sample_result->size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetCounterSamplesOnTarget(const astl_get_counter_samples_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle  = params->target_handle;
  const auto* counter_handle = params->counter_handle;
  auto*       samples        = params->samples;
  auto*       sample_count   = params->sample_count;
  if (!target_handle || !counter_handle || !samples || !sample_count || *sample_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  const auto* target = *result;

  auto get_counter_result = GetCounterFromHandle(counter_handle, target);
  if (!get_counter_result) {
    return get_counter_result.error();
  }
  const auto* counter = *get_counter_result;
  // create this span before modifying input *sample_count
  std::span<astl_sample_t> samples_span{samples, *sample_count};

  auto orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  astl::Orchestrator& orchestrator  = *(orchestrator_or_error.value().get());
  auto                sample_result = orchestrator.GetProcessedMetricSamples(counter, target);
  if (!sample_result) {
    return sample_result.error();
  }
  if (sample_result->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("orchestrator.GetProcessedMetricSamples reports absurdly large number of samples: {}",
                   sample_result->size());
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }
  *sample_count = static_cast<uint32_t>(sample_result->size());
  if (sample_result->size() > samples_span.size()) {
    *sample_count = 0;
    return ASTL_STATUS_COUNTER_SAMPLES_BUFFER_TOO_SMALL;
  }

  // helper lambda to convert a ProcessedSampledData into an astl_sample_t
  auto convert_to_counter_sample = [](const astl::ProcessedSampledData& processed_sample) {
    const auto union_value = processed_sample.value.ToAstlUnionValue().first;  // avoid constructing pair twice
    return astl_sample_t{.timestamp = static_cast<uint64_t>(processed_sample.timestamp.time_since_epoch().count()),
                         .value     = union_value};
  };

  // samples_span is at least large enough to accomodate sample_result because of above check.
  std::transform(sample_result->begin(), sample_result->end(), samples_span.begin(), convert_to_counter_sample);
  return ASTL_STATUS_SUCCESS;
}

/*** COLLECTED METRIC SAMPLES ***/
auto astlGetMetricSampleCountOnTarget(const astl_get_metric_sample_count_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       sample_count  = params->sample_count;
  if (!target_handle || !metric_handle || !sample_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  auto metric_or_error = GetMetricFromHandle(metric_handle, target_handle);
  if (!metric_or_error.has_value()) {
    ASTL_LOG_ERROR("GetProcessedSamples: Failed to get metric on target {} for handle {}", target->Name(),
                   metric_handle);
    return metric_or_error.error();
  }
  const astl::IMetric* metric = *metric_or_error;

  const auto get_samples_result = GetProcessedMetricSamples(metric, target);
  if (!get_samples_result) {
    return get_samples_result.error();
  }
  auto samples = *get_samples_result;

  if (samples.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("metric->GetProcessedSamples reports absurdly large number of samples: {}", samples.size());
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }
  *sample_count = static_cast<uint32_t>(samples.size());
  return ASTL_STATUS_SUCCESS;
}

auto astlGetMetricSamplesOnTarget(const astl_get_metric_samples_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       samples       = params->samples;
  auto*       sample_count  = params->sample_count;
  if (!target_handle || !metric_handle || !samples || !sample_count || *sample_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  auto get_metric_result = GetMetricFromHandle(metric_handle, target_handle);
  if (!get_metric_result) {
    return get_metric_result.error();
  }
  const auto* metric = *get_metric_result;

  auto collected_samples_or_error = GetProcessedMetricSamples(metric, target);
  if (!collected_samples_or_error) {
    return collected_samples_or_error.error();
  }
  const auto& collected_samples = *collected_samples_or_error;

  std::span<astl_sample_t> output_samples{samples, *sample_count};
  if (*sample_count < collected_samples.size()) {
    *sample_count = 0;
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }

  auto get_output_manager_result = GetOutputManager();
  if (!get_output_manager_result) {
    return get_output_manager_result.error();
  }
  auto* output_manager = *get_output_manager_result;

  auto create_buffer_result = output_manager->CreateBufferOutput(output_samples, sample_count);
  if (ASTL_STATUS_SUCCESS != create_buffer_result) {
    return create_buffer_result;
  }

  auto processed_samples_snapshot_or_error = GetProcessedSamplesSnapshot();
  if (!processed_samples_snapshot_or_error) {
    (void)output_manager->DestroyBufferOutput();
    return processed_samples_snapshot_or_error.error();
  }

  astl_status_code status = output_manager->OutputProcessedSamples(*processed_samples_snapshot_or_error,
                                                                   astl::OutputType::BUFFER, target, metric);
  // Intentionally ignore the result of DestroyBufferOutput; cleanup best-effort during sample retrieval.
  (void)output_manager->DestroyBufferOutput();
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

}  // namespace

auto astlGetMetricStatisticsOnTarget(const astl_get_metric_statistics_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       summary       = params->summary;
  if (!target_handle || !metric_handle || !summary) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Invalid argument(s)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  // Validate struct size for versioning
  if (summary->size != sizeof(astl_metric_statistics_t)) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Invalid summary struct size: {} (expected {})", summary->size,
                   sizeof(astl_metric_statistics_t));
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }

  const uint32_t k_allowed_summary_flags =
      (ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG | ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG);
  const uint32_t request_flags = summary->flags;
  if ((request_flags & ~k_allowed_summary_flags) != 0U) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if ((request_flags & ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG) != 0U &&
      (request_flags & ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG) != 0U) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const uint32_t selected_avg_mode = ((request_flags & ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG) != 0U)
                                         ? ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG
                                         : ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG;

  // Zero-initialise output fields so the struct contents are always deterministic,
  // regardless of whether the caller pre-initialised them.
  summary->flags = selected_avg_mode;
  summary->min   = {};
  summary->max   = {};
  summary->avg   = {};
  summary->count = 0;

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  auto get_metric_result = GetMetricFromHandle(metric_handle, target_handle);
  if (!get_metric_result) {
    return get_metric_result.error();
  }
  const auto* metric = *get_metric_result;

  // Get the processed samples for this metric
  auto samples_result = GetProcessedMetricSamples(metric, target);
  if (!samples_result) {
    return samples_result.error();
  }
  auto samples = *samples_result;

  // Get metric properties (needed for IsSupported check in MinMaxAvgSummarizer)
  astl_metric_props_t metric_properties{};
  metric_properties.size = sizeof(astl_metric_props_t);
  auto props_status      = metric->GetProperties(&metric_properties);
  if (props_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("astlGetMetricStatisticsOnTarget: Failed to get metric properties");
    return props_status;
  }

  const bool is_twa = (selected_avg_mode == ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG);

  // Always compute min/max/count; fill avg only for regular-average mode.
  auto minmax_result = ComputeMinMaxStats(samples, metric_properties);
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

  if (is_twa) {
    auto twa_result = ComputeTimeWeightedAvg(samples, target);
    if (!twa_result) {
      return twa_result.error();
    }
    if (twa_result->time_weighted_avg.has_value()) {
      summary->avg = twa_result->time_weighted_avg->ToAstlUnionValue().first;
    }
  }

  return ASTL_STATUS_SUCCESS;
}

namespace {

/**
 * @brief Shared helper: run the HistogramSummarizer (discrete mode) for the given
 *        target/metric and return the HistogramSummary, or an error status.
 */
auto ComputeDiscreteHistogram(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle)
    -> std::expected<astl::HistogramSummary, astl_status_code> {
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

  auto summary_result = summarizer.Summarize(*samples_result);
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
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       bin_count     = params->bin_count;
  if (!target_handle || !metric_handle || !bin_count) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramBinCountOnTarget: Invalid argument(s)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto histogram_result = ComputeDiscreteHistogram(target_handle, metric_handle);
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
  if (params->start_ts != 0 || params->end_ts != 0) {
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
  const auto* target_handle = params->target_handle;
  const auto* metric_handle = params->metric_handle;
  auto*       bins          = params->bins;
  auto*       bin_count     = params->bin_count;
  if (!target_handle || !metric_handle || !bins || !bin_count) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: Invalid argument(s)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  if (*bin_count == 0) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: bin_count must be > 0");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  std::span<astl_discrete_histogram_bin_t> bins_span{bins, *bin_count};

  // Validate struct size via the first element
  if (bins_span[0].size != sizeof(astl_discrete_histogram_bin_t)) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: Invalid bin struct size: {} (expected {})",
                   bins_span[0].size, sizeof(astl_discrete_histogram_bin_t));
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }

  auto histogram_result = ComputeDiscreteHistogram(target_handle, metric_handle);
  if (!histogram_result) {
    return histogram_result.error();
  }

  const auto& internal_bins = histogram_result->bins;
  if (internal_bins.size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: bin count exceeds uint32_t max: {}", internal_bins.size());
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  uint32_t required_count = static_cast<uint32_t>(internal_bins.size());

  if (*bin_count < required_count) {
    ASTL_LOG_ERROR("astlGetMetricDiscreteHistogramOnTarget: bin array too small (capacity={}, required={})", *bin_count,
                   required_count);
    *bin_count = required_count;
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }

  // Fill the caller's array
  for (uint32_t i = 0; i < required_count; ++i) {
    bins_span[i].size  = sizeof(astl_discrete_histogram_bin_t);
    bins_span[i].value = internal_bins[i].value.ToAstlUnionValue().first;
    bins_span[i].count = static_cast<uint64_t>(internal_bins[i].count);
  }

  *bin_count = required_count;
  return ASTL_STATUS_SUCCESS;
}

/***********************************************************************************
 **********************     POST-COLLECTION PROCESSING      ************************
 **********************************************************************************/

namespace {

/// Validate the caller-supplied windows array shared by both crop APIs.
auto ValidateCropWindows(const astl_crop_window_t* windows, uint32_t window_count) noexcept -> astl_status_code {
  if (windows == nullptr) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (window_count == 0) {
    ASTL_LOG_ERROR("astlCropSamples: window_count must be >= 1");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  std::span<const astl_crop_window_t> windows_span{windows, window_count};
  if (windows_span.front().size != sizeof(astl_crop_window_t)) {
    return ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE;
  }

  uint32_t window_index = 0;
  for (const auto& window : windows_span) {
    if (window.flags != 0U) {
      ASTL_LOG_ERROR("astlCropSamples: windows[{}].flags must be 0", window_index);
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    if (window.start_ts != 0 && window.end_ts != 0 && window.start_ts > window.end_ts) {
      ASTL_LOG_ERROR("astlCropSamples: windows[{}] has start_ts ({}) > end_ts ({})", window_index, window.start_ts,
                     window.end_ts);
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    ++window_index;
  }
  return ASTL_STATUS_SUCCESS;
}

}  // namespace

auto astlCropSamplesOnTarget(const astl_crop_samples_on_target_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (!params->target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto windows_status = ValidateCropWindows(params->windows, params->window_count);
  if (windows_status != ASTL_STATUS_SUCCESS) {
    return windows_status;
  }
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

auto astlCropMetricSamplesOnTarget(const astl_crop_metric_samples_on_target_params_t* params) noexcept
    -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }
  if (!params->target_handle || !params->metric_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  const auto windows_status = ValidateCropWindows(params->windows, params->window_count);
  if (windows_status != ASTL_STATUS_SUCCESS) {
    return windows_status;
  }

  return ASTL_STATUS_NOT_IMPLEMENTED;
}

auto astlCropSamples(const astl_crop_samples_params_t* params) noexcept -> astl_status_code {
  std::lock_guard<std::mutex> api_lock{GetCApiMutex()};
  const auto                  params_status = ValidateApiParams(params);
  if (params_status != ASTL_STATUS_SUCCESS) {
    return params_status;
  }

  const auto windows_status = ValidateCropWindows(params->windows, params->window_count);
  if (windows_status != ASTL_STATUS_SUCCESS) {
    return windows_status;
  }

  return ASTL_STATUS_NOT_IMPLEMENTED;
}
