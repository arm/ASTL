#include <algorithm>
#include <expected>
#include <iterator>
#include <limits>
#include <span>
#include <variant>

#include "astl/astl.h"
#include "astl_impl.hpp"
#include "counter.hpp"
#include "target.hpp"

/***********************************************************************************
 **********************               HELPERS               ************************
 **********************************************************************************/

std::expected<astl::ITarget*, astl_status_code> GetTargetFromHandle(astl_target_handle_t target_handle) {
  if (!target_handle) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  auto const& available_targets = astl::Orchestrator::GetInstance()->GetTargets();
  auto*       target            = static_cast<astl::ITarget*>(target_handle);
  auto        is_handle_match   = [target](auto& target_iter) -> bool { return target_iter.get() == target; };

  using std::begin, std::end;
  if (std::any_of(begin(available_targets), end(available_targets), is_handle_match)) {
    return target;
  }
  return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
}

std::expected<astl::ICounter*, astl_status_code> GetCounterFromHandle(astl_counter_handle_t counter_handle,
                                                                      const astl::ITarget*  target) {
  if (!counter_handle) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (!target) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  auto const&     available_counters = target->GetCounters();
  astl::ICounter* counter            = static_cast<astl::ICounter*>(counter_handle);
  auto            is_handle_match    = [counter](auto& counter_iter) -> bool { return counter_iter.get() == counter; };

  using std::begin, std::end;
  if (std::any_of(begin(available_counters), end(available_counters), is_handle_match)) {
    return counter;
  }
  return std::unexpected(ASTL_STATUS_INVALID_COUNTER_HANDLE);
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

astl_status_code astlGetTargetCount(uint32_t* target_count) {
  if (!target_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto targets_size = astl::Orchestrator::GetInstance()->GetTargets().size();
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
std::expected<VersionedPropertiesSpan, astl_status_code> GetVersionedTargetPropertiesSpan(
    astl_target_properties_t* targets, uint32_t target_count) {
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

astl_status_code astlGetTargets(astl_target_properties_t* targets, uint32_t* target_count) {
  if (!targets) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!target_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (*target_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto const& available_targets       = astl::Orchestrator::GetInstance()->GetTargets();
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
            *target_count = i;
            return result;
          }
        }
        *target_count = available_targets.size();
        return available_targets.size() == target_properties.size() ? ASTL_STATUS_SUCCESS
                                                                    : ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED;
      },
      *target_span);
}

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/

astl_status_code astlGetCounterCount(astl_target_handle_t target_handle, uint32_t* counter_count) {
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!counter_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  auto* target = *result;

  const auto num_counters = target->GetCounterCount();
  if (num_counters > std::numeric_limits<uint32_t>::max()) {
    return ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL;
  }
  *counter_count = static_cast<uint32_t>(num_counters);
  return ASTL_STATUS_SUCCESS;
}

astl_status_code astlGetCounters(astl_target_handle_t target_handle, astl_counter_properties_t* counters,
                                 uint32_t* counter_count) {
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!counters) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (!counter_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (*counter_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto counters_buffer_size = *counter_count;
  *counter_count            = 0;  // in case there's an error to return

  auto get_target_result = GetTargetFromHandle(target_handle);
  if (!get_target_result) {
    return get_target_result.error();
  }
  auto* target = *get_target_result;

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

astl_status_code astlGetMetricCount(astl_target_handle_t target_handle, uint32_t* metric_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetMetrics(astl_target_handle_t target_handle, astl_metric_properties_t* metrics,
                                uint32_t* metric_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/***********************************************************************************
 **********************              METRIC GROUPS             *********************
 **********************************************************************************/

astl_status_code astlGetMetricGroupCount(astl_target_handle_t target_handle, uint32_t* metric_group_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetMetricGroups(astl_target_handle_t target_handle, astl_metric_group_properties_t* metric_groups,
                                     uint32_t* metric_group_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetMetricGroupMetrics(astl_target_handle_t            target_handle,
                                           astl_metric_group_properties_t* metric_groups,
                                           astl_metric_properties_t*       metrics) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/***********************************************************************************
 **********************              COLLECTION                *********************
 **********************************************************************************/

/*** CONFIGURE COUNTERS ***/
astl_status_code astlConfigureCounterCollectionOnTarget(astl_target_handle_t                target_handle,
                                                        const astl_collection_parameters_t* collection_params,
                                                        const astl_counter_handle_t*        counter_handles,
                                                        uint32_t                            counter_count) {
  if (!target_handle || !collection_params || !counter_handles) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (counter_count == 0) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  auto* target = *result;

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
  std::vector<astl::ICounter*>           counters;
  counters.reserve(counter_count);
  for (const auto& handle : counter_handle_span) {
    auto get_counter_result = GetCounterFromHandle(handle, target);
    if (!get_counter_result) {
      return get_counter_result.error();
    }
    counters.push_back(*get_counter_result);
  }
  return target->ConfigureCounterCollection(collection_params, counters);
}

astl_status_code astlConfigureCounterCollection(const astl_collection_parameters_t* collection_params,
                                                const astl_counter_handle_t* counter_handles, uint32_t counter_count) {
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
  std::vector<astl::ICounter*>           counters;
  std::span<const astl_counter_handle_t> counter_handle_span{counter_handles, counter_count};
  std::transform(begin(counter_handle_span), std::end(counter_handle_span), std::back_inserter(counters),
                 [](const auto& counter_handle) { return static_cast<astl::ICounter*>(counter_handle); });

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
astl_status_code astlConfigureMetricCollectionOnTarget(astl_target_handle_t          target_handle,
                                                       astl_collection_parameters_t* collection_params,
                                                       astl_metric_handle_t* metric_handles, uint32_t metric_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlConfigureMetricCollection(astl_collection_parameters_t* collection_params,
                                               astl_metric_handle_t* metric_handles, uint32_t metric_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/*** CONFIGURE METRIC GROUPS ***/
astl_status_code astlConfigureMetricGroupCollectionOnTarget(astl_target_handle_t          target_handle,
                                                            astl_collection_parameters_t* collection_params,
                                                            astl_metric_group_handle_t*   metric_group_handles,
                                                            uint32_t                      metric_group_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlConfigureMetricGroupCollection(astl_collection_parameters_t* collection_params,
                                                    astl_metric_group_handle_t*   metric_group_handles,
                                                    uint32_t                      metric_group_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlReadImmediateOnTarget(astl_target_handle_t target_handle) {
  if (!target_handle) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  auto* target = *result;

  // TODO(https://jira.arm.com/browse/ASTL-54)  -- dispatch read to the metric manager instead of the target
  return target->ReadImmediate();
}

astl_status_code astlReadImmediate() {
  auto& available_targets = astl::Orchestrator::GetInstance()->GetTargets();
  for (auto& target : available_targets) {
    // TODO(https://jira.arm.com/browse/ASTL-54)  -- dispatch read to the metric manager instead of the target
    auto result = target->ReadImmediate();
    if (result != ASTL_STATUS_SUCCESS) {
      return result;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

astl_status_code astlStartCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlStartCollection() {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlPauseCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlPauseCollection() {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlResumeCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlResumeCollection() {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlStopCollectionOnTarget(astl_target_handle_t target_handle) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlStopCollection() {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/*** COLLECTED COUNTER SAMPLES ***/
astl_status_code astlGetCounterSampleCountOnTarget(astl_target_handle_t  target_handle,
                                                   astl_counter_handle_t counter_handle, uint32_t* sample_count) {
  if (!target_handle || !counter_handle || !sample_count) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto result = GetTargetFromHandle(target_handle);
  if (!result) {
    return result.error();
  }
  auto* target = *result;

  auto get_counter_result = GetCounterFromHandle(counter_handle, target);
  if (!get_counter_result) {
    return get_counter_result.error();
  }
  auto* counter = *get_counter_result;

  auto sample_result = target->GetCounterSampleCount(counter);
  if (!sample_result) {
    return sample_result.error();
  }
  *sample_count = *sample_result;
  return ASTL_STATUS_SUCCESS;
}

astl_status_code astlGetCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                               astl_counter_sample_t* samples, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllCounterSampleCountOnTarget(astl_target_handle_t target_handle, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_sample_t* samples,
                                                  uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllCounterSampleCount(uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllCounterSamples(astl_counter_sample_t* samples, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/*** COLLECTED METRIC SAMPLES ***/
astl_status_code astlGetMetricSampleCountOnTarget(astl_target_handle_t target_handle,
                                                  astl_metric_handle_t metric_handle, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetMetricSamplesOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                              astl_metric_sample_t* samples, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllMetricSampleCountOnTarget(astl_target_handle_t target_handle, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllMetricSamplesOnTarget(astl_target_handle_t target_handle, astl_metric_sample_t* samples,
                                                 uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllMetricSampleCount(uint32_t* metric_sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

astl_status_code astlGetAllMetricSamples(astl_metric_sample_t* metric_samples, uint32_t* sample_count) {
  astl_status_code result{ASTL_STATUS_NOT_IMPLEMENTED};
  return result;
}

/***********************************************************************************
 **********************              TEST                      *********************
 **********************************************************************************/
// TODO(https://github.com/Arm-Debug/ASTL/pull/17) - delete
astl_status_code astlTest() {
  astl_status_code result = astl::Orchestrator::Test();
  return result;
}
