#include <algorithm>
#include <expected>
#include <iterator>
#include <limits>
#include <span>
#include <variant>

#include "astl/astl.h"
#include "astl_impl.hpp"
#include "common/astl_defines.hpp"
#include "counter.hpp"
#include "metric/i_metric.hpp"
#include "output/i_output_manager.hpp"
#include "target.hpp"

/***********************************************************************************
 **********************               HELPERS               ************************
 **********************************************************************************/

/** @brief Confirms that the given non-null target_handle matches some known ITarget */
auto GetTargetFromHandle(astl_target_handle_t target_handle) -> std::expected<const astl::ITarget*, astl_status_code> {
  auto const& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
  }
  auto const& available_targets = orchestrator->GetTargets();
  const auto* target            = static_cast<const astl::ITarget*>(target_handle);
  auto        is_handle_match   = [target](auto& target_iter) -> bool { return target_iter.get() == target; };

  using std::begin, std::end;
  if (std::any_of(begin(available_targets), end(available_targets), is_handle_match)) {
    return target;
  }
  return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
}

auto GetCounterFromHandle(astl_counter_handle_t counter_handle, const astl::ITarget* target)
    -> std::expected<const astl::ICounter*, astl_status_code> {
  if (!counter_handle) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (!target) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  auto const&           available_counters = target->GetCounters();
  const astl::ICounter* counter            = static_cast<const astl::ICounter*>(counter_handle);
  auto is_handle_match = [counter](auto& counter_iter) -> bool { return counter_iter.get() == counter; };

  using std::begin, std::end;
  if (std::any_of(begin(available_counters), end(available_counters), is_handle_match)) {
    return counter;
  }
  return std::unexpected(ASTL_STATUS_INVALID_COUNTER_HANDLE);
}

auto GetMetricManager() -> std::expected<astl::IMetricManager*, astl_status_code> {
  auto const& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
  }
  const auto& metric_manager = orchestrator->GetMetricManager();
  if (!metric_manager) {
    ASTL_LOG_ERROR("No metric manager assigned to orchestrator");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return metric_manager.get();
}

auto GetOutputManager() -> std::expected<astl::IOutputManager*, astl_status_code> {
  auto const& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
  }
  const auto& output_manager = orchestrator->GetOutputManager();
  if (!output_manager) {
    ASTL_LOG_ERROR("No output manager assigned to orchestrator");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return output_manager.get();
}

auto GetMetricFromHandle(astl_metric_handle_t metric_handle, astl_target_handle_t target_handle)
    -> std::expected<const astl::IMetric*, astl_status_code> {
  auto const& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
  }

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
    ASTL_LOG_ERROR("GetProcessedSamples: Failed to get metric on target {} for handle {}", target->Name(),
                   metric_handle);
    return std::unexpected{metric_or_error.error()};
  }
  const astl::IMetric* metric = *metric_or_error;

  return metric;
}

auto GetProcessedMetricSamples(const astl::IMetric* metric, const astl::ITarget* target)
    -> std::expected<std::span<const astl::ProcessedSampledData>, astl_status_code> {
  auto const& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
  }
  auto samples_result = orchestrator->GetProcessedMetricSamples(metric, target);

  return samples_result;
}

auto GetProcessedSamples() -> std::expected<std::reference_wrapper<astl::ProcessedSamplesMap>, astl_status_code> {
  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return std::unexpected(ASTL_STATUS_NOT_INITIALIZED);
  }
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
  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
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
  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }

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
  return std::visit(
      [&available_targets, target_count](auto&& target_properties) {
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
      },
      *target_span);
}

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/
// TODO(ASTL-180) counter should be re-implemented as just a RawMetric.
auto astlGetCounterCount(astl_target_handle_t target_handle, uint32_t* counter_count) -> astl_status_code {
  if (!target_handle || !counter_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  const auto* target = *result;

  const auto num_counters = target->GetCounterCount();
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
  auto counters_buffer_size = *counter_count;
  *counter_count            = 0;  // in case there's an error to return

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  const auto* target = *get_target_result;

  const auto& available_counters = target->GetCounters();
  if (available_counters.size() > counters_buffer_size) {
    *counter_count = 0;
    return ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL;
  }
  if (available_counters.empty()) {
    *counter_count = 0;
    return ASTL_STATUS_NO_COUNTERS_FOUND;
  }
  std::span<astl_counter_properties_t> counter_span{counters, counters_buffer_size};
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
    if (auto result = available_counters[i]->GetProperties(&counter_span[i]); result != ASTL_STATUS_SUCCESS) {
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
  const auto* target = *get_target_result;

  const auto available_metrics = metric_manager->GetAvailableMetrics(target);
  if (!available_metrics) {
    return available_metrics.error();
  }
  const auto num_metrics = available_metrics->size();
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
  (void)target_handle;
  if (!metric_group_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  *metric_group_count = 0;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlGetMetricGroups(astl_target_handle_t target_handle, astl_metric_group_properties_t* metric_groups,
                         uint32_t* metric_group_count) -> astl_status_code {
  (void)target_handle;
  if (!metric_groups) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!metric_group_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::span<astl_metric_group_properties_t> metric_groups_span{metric_groups, *metric_group_count};
  // change when implementing this function
  metric_groups_span[0]._handle = nullptr;  // cppcheck-suppress unreadVariable
  *metric_group_count           = 0;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlGetMetricGroupMetrics(astl_target_handle_t target_handle, const astl_metric_group_properties_t* metric_groups,
                               astl_metric_properties_t* metrics) -> astl_status_code {
  (void)target_handle;
  if (!metric_groups || !metrics || metric_groups->_metric_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::span<astl_metric_properties_t> metrics_span{metrics, metric_groups->_metric_count};
  // change when implementing this function
  metrics_span[0]._handle = nullptr;  // cppcheck-suppress unreadVariable
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
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
  const auto* target = *result;

  if (counter_count > target->GetCounterCount()) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (sizeof(astl_collection_parameters_t) < collection_params->_size) {
    return ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  if (sizeof(astl_collection_parameters_t) > collection_params->_size) {
    return ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION;
  }
  std::span<const astl_counter_handle_t> counter_handle_span{counter_handles, counter_count};
  std::vector<const astl::ICounter*>     counters;
  counters.reserve(counter_count);
  for (const auto& handle : counter_handle_span) {
    auto get_counter_result = GetCounterFromHandle(handle, target);
    if (!get_counter_result) {
      return get_counter_result.error();
    }
    counters.push_back(*get_counter_result);
  }
  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
  return orchestrator->ConfigureCounterCollection(target, collection_params, counters);
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
  auto&                           orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
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
  (void)target_handle;
  (void)collection_params;
  (void)metric_group_handles;
  (void)metric_group_count;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

auto astlConfigureMetricGroupCollection(astl_collection_parameters_t* collection_params,
                                        astl_metric_group_handle_t* metric_group_handles, uint32_t metric_group_count)
    -> astl_status_code {
  (void)collection_params;
  (void)metric_group_handles;
  (void)metric_group_count;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
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

  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
  return orchestrator->ReadImmediate(target);
}

auto astlReadImmediate() -> astl_status_code {
  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
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

  const auto* target       = *result;
  auto&       orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
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

  const auto* target       = *result;
  auto&       orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
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
  const auto* counter = *get_counter_result;

  auto& orchestrator = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    return ASTL_STATUS_NOT_INITIALIZED;
  }
  auto sample_result = orchestrator->GetCounterSampleCount(target, counter);
  if (!sample_result) {
    return sample_result.error();
  }
  *sample_count = *sample_result;
  return ASTL_STATUS_SUCCESS;
}

auto astlGetCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                   astl_counter_sample_t* samples, uint32_t* sample_count) -> astl_status_code {
  (void)target_handle;
  (void)counter_handle;
  if (!samples) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!sample_count || *sample_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::span<astl_counter_sample_t> samples_span{samples, *sample_count};

  samples_span[0]._size       = sizeof(astl_counter_sample_t);
  samples_span[0]._timestamp  = 0U;
  samples_span[0]._value.ui64 = 0ULL;  // cppcheck-suppress unreadVariable
  *sample_count               = 0;
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
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
