#include <algorithm>
#include <expected>
#include <iterator>
#include <limits>
#include <span>
#include <variant>

#include "astl/astl.h"
#include "common/astl_defines.hpp"
#include "metric/counter.hpp"
#include "metric/i_metric.hpp"
#include "metric/i_metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/i_output_manager.hpp"
#include "target.hpp"

/***********************************************************************************
 **********************               HELPERS               ************************
 **********************************************************************************/

/** @brief Confirms that the given non-null target_handle matches some known ITarget */
auto GetTargetFromHandle(astl_target_handle_t target_handle) -> std::expected<const astl::ITarget*, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator      = orchestrator_or_error->get();
  auto const& available_targets = orchestrator->GetTargets();
  const auto* target            = static_cast<const astl::ITarget*>(target_handle);
  auto        is_handle_match   = [target](auto& target_iter) -> bool { return target_iter.get() == target; };

  using std::begin, std::end;
  if (std::any_of(begin(available_targets), end(available_targets), is_handle_match)) {
    return target;
  }
  return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
}

auto GetMetricManager() -> std::expected<astl::IMetricManager*, astl_status_code> {
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

auto GetOutputManager() -> std::expected<astl::IOutputManager*, astl_status_code> {
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

auto GetCounterFromHandle(astl_counter_handle_t counter_handle, const astl::ITarget* target)
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

auto GetMetricFromHandle(astl_metric_handle_t metric_handle, astl_target_handle_t target_handle)
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

auto GetProcessedMetricSamples(const astl::IMetric* metric, const astl::ITarget* target)
    -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator = orchestrator_or_error->get();

  auto samples_result = orchestrator->GetProcessedMetricSamples(metric, target);

  return samples_result;
}

auto GetProcessedSamples() -> std::expected<std::reference_wrapper<astl::ProcessedSamplesMap>, astl_status_code> {
  auto const& orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return std::unexpected{orchestrator_or_error.error()};
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return orchestrator->GetProcessedSamples();
}

constexpr uint32_t kFirstElementIdx{0};

// Used to get the '_size' field of the first element in the span, array, etc of astl_target_properties_t or other
// structs
template <typename Container>
auto GetFirstElementSizeField(Container const& elements)
    -> std::expected<decltype(elements[kFirstElementIdx]._size), astl_status_code> {
  if (std::size(elements) == 0) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return elements[kFirstElementIdx]._size;
}

/***********************************************************************************
 **********************               TARGETS               ************************
 **********************************************************************************/

auto astlGetTargetCount(uint32_t* target_count) -> astl_status_code {
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

using VersionedPropertiesSpan = std::variant<std::span<astl_target_properties_t> >;

/**
 * @brief retrieve either an error code or a span of some version of astl_target_properties
 *
 * If/when we add a field to astl_target_properties_t, we'll keep a copy of that original
 * declaration and rename it astl_target_properties_v1_t so we can provide overloaded functions
 * for it.
 */
auto GetVersionedTargetPropertiesSpan(astl_target_properties_t* targets, uint32_t target_count)
    -> std::expected<VersionedPropertiesSpan, astl_status_code> {
  // at first, assume the caller's targets are the same size as the astl_target_properties_t struct in our header.
  std::span<astl_target_properties_t> target_span{targets, target_count};
  auto                                given_struct_size = GetFirstElementSizeField(target_span);
  if (!given_struct_size) {
    return std::unexpected(given_struct_size.error());
  }
  switch (*given_struct_size) {
    case sizeof(astl_target_properties_t):
      return target_span;
    // future extension:
    // case sizeof(astl_target_properties_v1_t):
    // return std::span<astl_target_properties_v1_t>(targets, target_count);
    default:
      if (sizeof(astl_target_properties_t) > *given_struct_size) {
        // if we add elements to the astl_target_properties_t in the future,
        // we'll have an opportunity to add backwards-compatibility code here
        return std::unexpected(ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION);
      } else {
        // in this case, the caller's version of the struct is newer than ours
        // and we don't know how to handle it. We _could_ only touch the
        // fields we know about, but for now we'll return an error
        return std::unexpected(ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION);
      }
  }
}

auto astlGetTargets(astl_target_properties_t* targets, uint32_t* target_count) -> astl_status_code {
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
  // convert our raw pointer to _some_ version of astl_target_properties_t into a std::span
  // of a specific struct, maybe astl_target_properties_t, maybe astl_target_properties_v0_t,
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
auto astlGetCounterCount(astl_target_handle_t target_handle, uint32_t* counter_count) -> astl_status_code {
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

auto astlGetCounters(astl_target_handle_t target_handle, astl_counter_properties_t* counters, uint32_t* counter_count)
    -> astl_status_code {
  if (!target_handle || !counters || !counter_count || *counter_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto                           counter_buffer_size  = *counter_count;
  auto                                 counters_buffer_size = *counter_count;
  std::span<astl_counter_properties_t> output_counters{counters, *counter_count};
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
  std::span<astl_counter_properties_t> counter_span{counters, counter_buffer_size};
  auto                                 counter_struct_size = GetFirstElementSizeField(counter_span);
  if (!counter_struct_size) {
    return counter_struct_size.error();
  }
  if (*counter_struct_size < sizeof(astl_counter_properties_t)) {
    // if we add elements to the astl_counter_properties_t in the future,
    // we'll have an opportunity to add backwards-compatibility code here
    return ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION;
  }
  if (*counter_struct_size > sizeof(astl_counter_properties_t)) {
    // caller's version of the struct is newer than ours
    // We _could_ only touch the fields we know about, but for now we'll return an error
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

auto astlGetMetricCount(astl_target_handle_t target_handle, uint32_t* metric_count) -> astl_status_code {
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

auto astlGetMetrics(astl_target_handle_t target_handle, astl_metric_properties_t* metrics, uint32_t* metric_count)
    -> astl_status_code {
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

  std::span<astl_metric_properties_t> output_metrics{metrics, *metric_count};
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
  // check struct size of astl_metric_properties_t for ABI compatibility
  auto metric_struct_size = GetFirstElementSizeField(output_metrics);
  if (!metric_struct_size) {
    return metric_struct_size.error();
  }
  if (*metric_struct_size < sizeof(astl_metric_properties_t)) {
    return ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION;
  }
  if (*metric_struct_size > sizeof(astl_metric_properties_t)) {
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
 **********************              METRIC GROUPS             *********************
 **********************************************************************************/

auto astlGetMetricGroupCount(astl_target_handle_t target_handle, uint32_t* metric_group_count) -> astl_status_code {
  if (!target_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupCount: target_handle is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
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

  ASTL_LOG_TRACE("astlGetMetricGroupCount: getting target from handle");
  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  ASTL_LOG_TRACE("astlGetMetricGroupCount: getting available metric groups");
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

auto astlGetMetricGroups(astl_target_handle_t target_handle, astl_metric_group_properties_t* metric_groups,
                         uint32_t* metric_group_count) -> astl_status_code {
  if (!target_handle || !metric_groups || !metric_group_count || *metric_group_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto metric_groups_properties = std::span<astl_metric_group_properties_t>{metric_groups, *metric_group_count};
  *metric_group_count           = 0;  // in case there's an error to return
  if (metric_groups_properties[0]._size < sizeof(astl_metric_group_properties_t)) {
    return ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION;
  }
  if (metric_groups_properties[0]._size > sizeof(astl_metric_group_properties_t)) {
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
  // for each metric group, copy its properties to the provided 'metric_groups' buffer
  for (size_t i = 0; i < std::min(groups_result->size(), metric_groups_properties.size()); ++i) {
    auto result = metric_manager->GetMetricGroupProperties((*groups_result)[i], &metric_groups_properties[i]);
    if (result != ASTL_STATUS_SUCCESS) {
      *metric_group_count = static_cast<uint32_t>(i);
      return result;
    }
  }
  auto result = metric_groups_properties.size() > groups_result->size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
                                                                        : ASTL_STATUS_SUCCESS;
  return result;
}

auto astlGetMetricGroupMetrics(astl_target_handle_t target_handle, const astl_metric_group_properties_t* metric_group,
                               astl_metric_properties_t* metrics) -> astl_status_code {
  if (!target_handle) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: target_handle cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  (void)target_handle;  // unused, since the metrics in a group don't actually depend on the group's target
  if (!metric_group) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metric_group cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metrics) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: metrics ptr cannot be null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (metric_group->_metric_count == 0) {
    ASTL_LOG_ERROR("astlGetMetricGroupMetrics: _metric_count cannot be 0");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto get_metric_manager_result = GetMetricManager();
  if (!get_metric_manager_result) {
    return get_metric_manager_result.error();
  }
  auto*                               metric_manager = *get_metric_manager_result;
  std::span<astl_metric_properties_t> metrics_properties{metrics, metric_group->_metric_count};
  if (metrics_properties[0]._size < sizeof(astl_metric_properties_t)) {
    return ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION;
  }
  if (metrics_properties[0]._size > sizeof(astl_metric_properties_t)) {
    return ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION;
  }

  ASTL_LOG_TRACE("astlGetMetricGroupMetrics: getting metrics in group");
  auto metrics_in_group = metric_manager->GetMetricsInGroup(metric_group);
  if (!metrics_in_group) {
    return metrics_in_group.error();
  }

  if (metrics_properties.size() < metrics_in_group->size()) {
    return ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL;
  }
  if (metrics_in_group->size() > std::numeric_limits<uint32_t>::max()) {
    ASTL_LOG_ERROR("metric_manager->GetMetricsInGroup reports absurdly large number of metrics: {}",
                   metrics_in_group->size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  // for each metric in the group, copy its properties into the provided `metrics` buffer
  size_t idx = 0;
  std::ranges::for_each_n(metrics_in_group->begin(), static_cast<std::ptrdiff_t>(metrics_properties.size()),
                          [&idx, metric_manager, &metrics_properties](const auto& metric_handle) {
                            auto status = metric_manager->GetProperties(metric_handle, &metrics_properties[idx]);
                            if (status != ASTL_STATUS_SUCCESS) {
                              ASTL_LOG_ERROR("Failed to get properties for metric {}: {}", metric_handle, status);
                            }
                            ++idx;
                          });
  return metrics_properties.size() > metrics_in_group->size() ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
                                                              : ASTL_STATUS_SUCCESS;
}

/***********************************************************************************
 **********************              COLLECTION                *********************
 **********************************************************************************/

/*** CONFIGURE COUNTERS ***/
auto astlConfigureCounterCollectionOnTarget(astl_target_handle_t                target_handle,
                                            const astl_collection_parameters_t* collection_params,
                                            const astl_counter_handle_t* counter_handles, uint32_t counter_count)
    -> astl_status_code {
  if (!target_handle || !collection_params || !counter_handles || counter_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
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
  if (sizeof(astl_collection_parameters_t) < collection_params->_size) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (sizeof(astl_collection_parameters_t) > collection_params->_size) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  std::span<const astl_counter_handle_t> counter_handle_span{counter_handles, counter_count};
  auto const&                            orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();

  return orchestrator->ConfigureCounterCollection(target, collection_params, counter_handle_span);
}

auto astlConfigureCounterCollection(const astl_collection_parameters_t* collection_params,
                                    const astl_counter_handle_t* counter_handles, uint32_t counter_count)
    -> astl_status_code {
  if (!collection_params || !counter_handles) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (counter_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (sizeof(astl_collection_parameters_t) < collection_params->_size) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (sizeof(astl_collection_parameters_t) > collection_params->_size) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
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
auto astlConfigureMetricCollectionOnTarget(astl_target_handle_t          target_handle,
                                           astl_collection_parameters_t* collection_params,
                                           astl_metric_handle_t* metric_handles, uint32_t metric_count)
    -> astl_status_code {
  // check input arguments for null and api version
  if (!target_handle || !collection_params || !metric_handles || !metric_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (collection_params->_size < sizeof(astl_collection_parameters_t)) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (collection_params->_size > sizeof(astl_collection_parameters_t)) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto*                     target = *get_target_result;
  std::span<astl_metric_handle_t> metric_handle_span{metric_handles, metric_count};
  auto const&                     orchestrator_or_error = astl::Orchestrator::GetInstance();
  if (!orchestrator_or_error) {
    return orchestrator_or_error.error();
  }
  const auto& orchestrator = orchestrator_or_error->get();
  return orchestrator->ConfigureMetricCollection(target, collection_params, metric_handle_span);
}

auto astlConfigureMetricCollection(astl_collection_parameters_t* collection_params,
                                   astl_metric_handle_t* metric_handles, uint32_t metric_count) -> astl_status_code {
  (void)collection_params;
  (void)metric_handles;
  (void)metric_count;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/*** CONFIGURE METRIC GROUPS ***/
auto astlConfigureMetricGroupCollectionOnTarget(astl_target_handle_t          target_handle,
                                                astl_collection_parameters_t* collection_params,
                                                astl_metric_group_handle_t*   metric_group_handles,
                                                uint32_t                      metric_group_count) -> astl_status_code {
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
  const auto* target = *get_target_result;
  // for each group, push all its metrics into the metric_handles_vector
  std::span<astl_metric_group_handle_t> metric_group_handle_span{metric_group_handles, metric_group_count};
  std::vector<astl_metric_handle_t>     metric_handles_vector;

  for (const auto& group_handle : metric_group_handle_span) {
    auto get_group_result = metric_manager->GetMetricsInGroup(group_handle);
    if (!get_group_result) {
      return get_group_result.error();
    }
    // ensure we have enough capacity
    if (metric_handles_vector.capacity() < metric_handles_vector.size() + get_group_result->size()) {
      // at least geometric growth to avoid O(n^2) copies, but also enough for all the new metrics
      auto new_size =
          std::max(metric_handles_vector.capacity() * 2, metric_handles_vector.size() + get_group_result->size());
      metric_handles_vector.reserve(new_size);
    }

    std::copy(std::begin(*get_group_result), std::end(*get_group_result), std::back_inserter(metric_handles_vector));
  }
  // collect on all the metrics we gathered from the given groups
  std::span<const astl_metric_handle_t> metric_handle_span{metric_handles_vector};
  return orchestrator->ConfigureMetricCollection(target, collection_params, metric_handle_span);
}

auto astlConfigureMetricGroupCollection(astl_collection_parameters_t* collection_params,
                                        astl_metric_group_handle_t* metric_group_handles, uint32_t metric_group_count)
    -> astl_status_code {
  (void)collection_params;
  (void)metric_group_handles;
  (void)metric_group_count;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  // @todo(ASTL-181) implement this function
  return result;
}

auto astlReadImmediateOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
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

auto astlReadImmediate() -> astl_status_code {
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

auto astlStartCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
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

auto astlStartCollection() -> astl_status_code {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlPauseCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  (void)target_handle;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlPauseCollection() -> astl_status_code {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlResumeCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  (void)target_handle;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlResumeCollection() -> astl_status_code {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlStopCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
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

auto astlStopCollection() -> astl_status_code {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/*** COLLECTED COUNTER SAMPLES ***/
auto astlGetCounterSampleCountOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                       uint32_t* sample_count) -> astl_status_code {
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

auto astlGetCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                   astl_counter_sample_t* samples, uint32_t* sample_count) -> astl_status_code {
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
  std::span<astl_counter_sample_t> samples_span{samples, *sample_count};
  auto                             callers_sample_struct_size = GetFirstElementSizeField(samples_span);
  if (!callers_sample_struct_size) {
    return callers_sample_struct_size.error();
  }
  if (*callers_sample_struct_size < sizeof(astl_counter_sample_t)) {
    ASTL_LOG_ERROR("astl_counter_sample_t struct too small: caller's size {}, expected size {}",
                   *callers_sample_struct_size, sizeof(astl_counter_sample_t));
    return ASTL_STATUS_OLD_COUNTER_SAMPLE_STRUCT_VERSION;
  }
  if (*callers_sample_struct_size > sizeof(astl_counter_sample_t)) {
    ASTL_LOG_ERROR("astl_counter_sample_t struct too large: caller's size {}, expected size {}",
                   *callers_sample_struct_size, sizeof(astl_counter_sample_t));
    return ASTL_STATUS_NEW_COUNTER_SAMPLE_STRUCT_VERSION;
  }

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

  // helper lambda to convert a ProcessedSampledData into an astl_counter_sample_t
  auto convert_to_counter_sample = [](const astl::ProcessedSampledData& processed_sample) {
    const auto union_value = processed_sample.value.ToAstlUnionValue().first;  // avoid constructing pair twice
    return astl_counter_sample_t{._size      = sizeof(astl_counter_sample_t),
                                 ._timestamp = processed_sample.timestamp.time_since_epoch().count(),
                                 ._value     = union_value};
  };

  // samples_span is at least large enough to accomodate sample_result because of above check.
  std::transform(sample_result->begin(), sample_result->end(), samples_span.begin(), convert_to_counter_sample);
  return ASTL_STATUS_SUCCESS;
}

/*** COLLECTED METRIC SAMPLES ***/
auto astlGetMetricSampleCountOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                      uint32_t* sample_count) -> astl_status_code {
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

auto astlGetMetricSamplesOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                  astl_metric_sample_t* samples, uint32_t* sample_count) -> astl_status_code {
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

  auto collected_samples_or_error = GetProcessedSamples();
  if (!collected_samples_or_error) {
    return collected_samples_or_error.error();
  }
  auto collected_samples = *collected_samples_or_error;  // reference_wrapper<ProcessedSamplesMap>

  std::span<astl_metric_sample_t> output_samples{samples, *sample_count};
  if (*sample_count < collected_samples.get().size()) {
    *sample_count = 0;
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }
  auto sample_struct_size = GetFirstElementSizeField(output_samples);
  if (!sample_struct_size) {
    return sample_struct_size.error();
  }
  if (*sample_struct_size < sizeof(astl_metric_sample_t)) {
    return ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION;
  }
  if (*sample_struct_size > sizeof(astl_metric_sample_t)) {
    return ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION;
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

  astl_status_code status =
      output_manager->OutputProcessedSamples(collected_samples.get(), astl::OutputType::BUFFER, target, metric);
  // Intentionally ignore the result of DestroyBufferOutput; cleanup best-effort during sample retrieval.
  (void)output_manager->DestroyBufferOutput();
  return status;
}
