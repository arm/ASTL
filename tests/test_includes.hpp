// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TEST_INCLUDES_HPP_
#define TEST_INCLUDES_HPP_

// This file #includes all necessary headers from test dependencies, such as catch2 and trompeloeil.
// importantly, it defines some StringMaker specializations for custom types used in the tests,
// which _must_ be defined before including some catch2 headers.

#include <catch2/catch_tostring.hpp>
#include <magic_enum/magic_enum.hpp>
#include <string>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"

/**
 * @brief Extend catch2's StringMaker to support astl_status_code. Include this header before including catch2.
 *
 */
namespace Catch {
template <>
struct StringMaker<astl_status_code> {
  static std::string convert(astl_status_code value) { return std::string(magic_enum::enum_name(value)); }
};

}  // namespace Catch

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/trompeloeil.hpp>

inline auto AstlGetSystemInfo(astl_platform_props_t* system_info) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_system_info_params_t, params, .flags = 0, .system_info = system_info);
  return astlGetSystemInfo(&params);
}

inline auto GetTargetCount(uint32_t* target_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_target_count_params_t, params, .flags = 0, .target_count = target_count);
  return astlGetTargetCount(&params);
}

inline auto GetTargets(astl_target_props_t* targets, uint32_t* target_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_targets_params_t, params, .flags = 0, .targets = targets, .target_count = target_count);
  return astlGetTargets(&params);
}

inline auto GetCounterCount(astl_target_handle_t target_handle, uint32_t* counter_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_counter_count_params_t, params, .flags = 0, .target_handle = target_handle,
                   .counter_count = counter_count);
  return astlGetCounterCount(&params);
}

inline auto GetCounters(astl_target_handle_t target_handle, astl_counter_props_t* counters, uint32_t* counter_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_counters_params_t, params, .flags = 0, .target_handle = target_handle, .counters = counters,
                   .counter_count = counter_count);
  return astlGetCounters(&params);
}

inline auto GetMetricCount(astl_target_handle_t target_handle, uint32_t* metric_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_count_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_count = metric_count);
  return astlGetMetricCount(&params);
}

inline auto GetMetrics(astl_target_handle_t target_handle, astl_metric_props_t* metrics, uint32_t* metric_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metrics_params_t, params, .flags = 0, .target_handle = target_handle, .metrics = metrics,
                   .metric_count = metric_count);
  return astlGetMetrics(&params);
}

inline auto GetMetricStateCountOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                        uint32_t* state_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_state_count_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_handle = metric_handle, .state_count = state_count);
  return astlGetMetricStateCountOnTarget(&params);
}

inline auto GetMetricStatesOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                    astl_state_props_t* states, uint32_t* state_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_states_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_handle = metric_handle, .states = states, .state_count = state_count);
  return astlGetMetricStatesOnTarget(&params);
}

inline auto GetMetricGroupCount(astl_target_handle_t target_handle, uint32_t* metric_group_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_group_count = metric_group_count);
  return astlGetMetricGroupCount(&params);
}

inline auto GetMetricGroups(astl_target_handle_t target_handle, astl_metric_group_props_t* metric_groups,
                            uint32_t* metric_group_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_groups = metric_groups, .metric_group_count = metric_group_count);
  return astlGetMetricGroups(&params);
}

inline auto GetMetricGroupMetrics(astl_target_handle_t target_handle, const astl_metric_group_props_t* metric_group,
                                  astl_metric_props_t* metrics) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_group = metric_group, .metrics = metrics);
  return astlGetMetricGroupMetrics(&params);
}

inline auto ConfigureCounterCollectionOnTarget(astl_target_handle_t            target_handle,
                                               const astl_collection_params_t* collection_params,
                                               const astl_counter_handle_t* counter_handles, uint32_t counter_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_configure_counter_collection_on_target_params_t, params, .flags = 0,
                   .target_handle = target_handle, .collection_params = collection_params,
                   .counter_handles = counter_handles, .counter_count = counter_count);
  return astlConfigureCounterCollectionOnTarget(&params);
}

inline auto ConfigureCounterCollection(const astl_collection_params_t* collection_params,
                                       const astl_counter_handle_t* counter_handles, uint32_t counter_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_configure_counter_collection_params_t, params, .flags = 0,
                   .collection_params = collection_params, .counter_handles = counter_handles,
                   .counter_count = counter_count);
  return astlConfigureCounterCollection(&params);
}

inline auto ConfigureMetricCollectionOnTarget(astl_target_handle_t            target_handle,
                                              const astl_collection_params_t* collection_params,
                                              const astl_metric_handle_t* metric_handles, uint32_t metric_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, params, .flags = 0,
                   .target_handle = target_handle, .collection_params = collection_params,
                   .metric_handles = metric_handles, .metric_count = metric_count);
  return astlConfigureMetricCollectionOnTarget(&params);
}

inline auto ConfigureMetricCollection(const astl_collection_params_t* collection_params,
                                      const astl_metric_handle_t* metric_handles, uint32_t metric_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_configure_metric_collection_params_t, params, .flags = 0,
                   .collection_params = collection_params, .metric_handles = metric_handles,
                   .metric_count = metric_count);
  return astlConfigureMetricCollection(&params);
}

inline auto ConfigureMetricGroupCollectionOnTarget(astl_target_handle_t              target_handle,
                                                   const astl_collection_params_t*   collection_params,
                                                   const astl_metric_group_handle_t* metric_group_handles,
                                                   uint32_t metric_group_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                   .target_handle = target_handle, .collection_params = collection_params,
                   .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
  return astlConfigureMetricGroupCollectionOnTarget(&params);
}

inline auto ConfigureMetricGroupCollection(const astl_collection_params_t*   collection_params,
                                           const astl_metric_group_handle_t* metric_group_handles,
                                           uint32_t                          metric_group_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_configure_metric_group_collection_params_t, params, .flags = 0,
                   .collection_params = collection_params, .metric_group_handles = metric_group_handles,
                   .metric_group_count = metric_group_count);
  return astlConfigureMetricGroupCollection(&params);
}

inline auto ReadImmediateOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_read_immediate_on_target_params_t, params, .flags = 0, .target_handle = target_handle);
  return astlReadImmediateOnTarget(&params);
}

inline auto ReadImmediate() -> astl_status_code {
  ASTL_INIT_STRUCT(astl_read_immediate_params_t, params, .flags = 0);
  return astlReadImmediate(&params);
}

inline auto StartCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_start_collection_on_target_params_t, params, .flags = 0, .target_handle = target_handle);
  return astlStartCollectionOnTarget(&params);
}

inline auto StartCollection() -> astl_status_code {
  ASTL_INIT_STRUCT(astl_start_collection_params_t, params, .flags = 0);
  return astlStartCollection(&params);
}

inline auto PauseCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_pause_collection_on_target_params_t, params, .flags = 0, .target_handle = target_handle);
  return astlPauseCollectionOnTarget(&params);
}

inline auto PauseCollection() -> astl_status_code {
  ASTL_INIT_STRUCT(astl_pause_collection_params_t, params, .flags = 0);
  return astlPauseCollection(&params);
}

inline auto ResumeCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_resume_collection_on_target_params_t, params, .flags = 0, .target_handle = target_handle);
  return astlResumeCollectionOnTarget(&params);
}

inline auto ResumeCollection() -> astl_status_code {
  ASTL_INIT_STRUCT(astl_resume_collection_params_t, params, .flags = 0);
  return astlResumeCollection(&params);
}

inline auto StopCollectionOnTarget(astl_target_handle_t target_handle) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_stop_collection_on_target_params_t, params, .flags = 0, .target_handle = target_handle);
  return astlStopCollectionOnTarget(&params);
}

inline auto StopCollection() -> astl_status_code {
  ASTL_INIT_STRUCT(astl_stop_collection_params_t, params, .flags = 0);
  return astlStopCollection(&params);
}

inline auto GetCounterSampleCountOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                          uint32_t* sample_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_counter_sample_count_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .counter_handle = counter_handle, .sample_count = sample_count, .start_ts = 0, .end_ts = 0);
  return astlGetCounterSampleCountOnTarget(&params);
}

inline auto GetCounterSamplesOnTarget(astl_target_handle_t target_handle, astl_counter_handle_t counter_handle,
                                      astl_sample_t* samples, uint32_t* sample_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_counter_samples_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .counter_handle = counter_handle, .samples = samples, .sample_count = sample_count, .start_ts = 0,
                   .end_ts = 0);
  return astlGetCounterSamplesOnTarget(&params);
}

inline auto GetMetricSampleCountOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                         uint32_t* sample_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_sample_count_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_handle = metric_handle, .sample_count = sample_count, .start_ts = 0, .end_ts = 0);
  return astlGetMetricSampleCountOnTarget(&params);
}

inline auto GetMetricSamplesOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                     astl_sample_t* samples, uint32_t* sample_count) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_samples_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_handle = metric_handle, .samples = samples, .sample_count = sample_count, .start_ts = 0,
                   .end_ts = 0);
  return astlGetMetricSamplesOnTarget(&params);
}

inline auto GetMetricStatisticsOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                        astl_metric_statistics_t* summary) -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_statistics_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                   .metric_handle = metric_handle, .summary = summary, .start_ts = 0, .end_ts = 0);
  return astlGetMetricStatisticsOnTarget(&params);
}

inline auto GetMetricDiscreteHistogramBinCountOnTarget(astl_target_handle_t target_handle,
                                                       astl_metric_handle_t metric_handle, uint32_t* bin_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_discrete_histogram_bin_count_on_target_params_t, params, .flags = 0,
                   .target_handle = target_handle, .metric_handle = metric_handle, .bin_count = bin_count,
                   .start_ts = 0, .end_ts = 0);
  return astlGetMetricDiscreteHistogramBinCountOnTarget(&params);
}

inline auto GetMetricDiscreteHistogramOnTarget(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle,
                                               astl_discrete_histogram_bin_t* bins, uint32_t* bin_count)
    -> astl_status_code {
  ASTL_INIT_STRUCT(astl_get_metric_discrete_histogram_on_target_params_t, params, .flags = 0,
                   .target_handle = target_handle, .metric_handle = metric_handle, .bins = bins, .bin_count = bin_count,
                   .start_ts = 0, .end_ts = 0);
  return astlGetMetricDiscreteHistogramOnTarget(&params);
}

#endif  // TEST_INCLUDES_HPP_
