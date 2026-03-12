// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../../test_includes.hpp"

namespace {

constexpr auto AlignUp(size_t value, size_t alignment) -> size_t {
  return ((value + alignment - 1U) / alignment) * alignment;
}

constexpr auto Max(size_t left_value, size_t right_value) -> size_t {
  return left_value > right_value ? left_value : right_value;
}

template <typename... Fields>
constexpr auto StructLayoutSize() -> size_t {
  size_t offset        = 0;
  size_t max_alignment = 1;
  ((offset        = AlignUp(offset, alignof(Fields)), offset += sizeof(Fields),
    max_alignment = Max(max_alignment, alignof(Fields))),
   ...);
  return AlignUp(offset, max_alignment);
}

template <typename T>
constexpr auto IsStdLayout() -> bool {
  return std::is_standard_layout_v<T>;
}

constexpr size_t kFlagsOffset    = sizeof(size_t);
constexpr size_t kFirstPtrOffset = AlignUp(sizeof(size_t) + sizeof(uint32_t), alignof(void*));

// Core data structs
static_assert(IsStdLayout<astl_platform_props_t>());
static_assert(offsetof(astl_platform_props_t, size) == 0);
static_assert(offsetof(astl_platform_props_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_platform_props_t) ==
              StructLayoutSize<size_t, uint32_t, const char*, const char*, const char*, const char*, const char*,
                               const char*, const char*, const char*, const char*>());

static_assert(IsStdLayout<astl_target_props_t>());
static_assert(offsetof(astl_target_props_t, size) == 0);
static_assert(
    sizeof(astl_target_props_t) ==
    StructLayoutSize<size_t, astl_target_handle_t, astl_target_handle_t, const char*, const char*, const char*>());

static_assert(IsStdLayout<astl_sample_t>());
static_assert(sizeof(astl_sample_t) == StructLayoutSize<uint64_t, astl_value_t>());

static_assert(IsStdLayout<astl_counter_props_t>());
static_assert(offsetof(astl_counter_props_t, size) == 0);
static_assert(offsetof(astl_counter_props_t, handle) == AlignUp(sizeof(size_t), alignof(astl_counter_handle_t)));
static_assert(sizeof(astl_counter_props_t) ==
              StructLayoutSize<size_t, astl_counter_handle_t, const char*, const char*, uint32_t, astl_units_t,
                               const char*, astl_value_type_t, astl_counter_type_t>());

static_assert(IsStdLayout<astl_metric_props_t>());
static_assert(offsetof(astl_metric_props_t, size) == 0);
static_assert(offsetof(astl_metric_props_t, handle) == AlignUp(sizeof(size_t), alignof(astl_metric_handle_t)));
static_assert(sizeof(astl_metric_props_t) ==
              StructLayoutSize<size_t, astl_metric_handle_t, const char*, const char*, uint32_t, astl_units_t,
                               astl_value_type_t, astl_metric_type_t, astl_category_t>());

static_assert(IsStdLayout<astl_state_props_t>());
static_assert(offsetof(astl_state_props_t, size) == 0);
static_assert(sizeof(astl_state_props_t) == StructLayoutSize<size_t, astl_value_t, const char*, const char*>());

static_assert(IsStdLayout<astl_metric_group_props_t>());
static_assert(offsetof(astl_metric_group_props_t, size) == 0);
static_assert(offsetof(astl_metric_group_props_t, handle) ==
              AlignUp(sizeof(size_t), alignof(astl_metric_group_handle_t)));
static_assert(
    sizeof(astl_metric_group_props_t) ==
    StructLayoutSize<size_t, astl_metric_group_handle_t, const char*, const char*, uint32_t, astl_metric_props_t*>());

static_assert(IsStdLayout<astl_collection_params_t>());
static_assert(offsetof(astl_collection_params_t, size) == 0);
static_assert(offsetof(astl_collection_params_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_collection_params_t) ==
              StructLayoutSize<size_t, uint32_t, uint32_t, astl_collection_mode_t>());

static_assert(IsStdLayout<astl_save_params_t>());
static_assert(offsetof(astl_save_params_t, size) == 0);
static_assert(offsetof(astl_save_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_save_params_t, output_file_path) == kFirstPtrOffset);
static_assert(sizeof(astl_save_params_t) == StructLayoutSize<size_t, uint32_t, const char*>());

static_assert(IsStdLayout<astl_load_params_t>());
static_assert(offsetof(astl_load_params_t, size) == 0);
static_assert(offsetof(astl_load_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_load_params_t, input_file_path) == kFirstPtrOffset);
static_assert(sizeof(astl_load_params_t) == StructLayoutSize<size_t, uint32_t, const char*, size_t>());

static_assert(IsStdLayout<astl_metric_statistics_t>());
static_assert(offsetof(astl_metric_statistics_t, size) == 0);
static_assert(offsetof(astl_metric_statistics_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_metric_statistics_t) ==
              StructLayoutSize<size_t, uint32_t, astl_value_t, astl_value_t, astl_value_t, uint64_t>());

static_assert(IsStdLayout<astl_discrete_histogram_bin_t>());
static_assert(offsetof(astl_discrete_histogram_bin_t, size) == 0);
static_assert(sizeof(astl_discrete_histogram_bin_t) == StructLayoutSize<size_t, astl_value_t, uint64_t>());

// API parameter structs
static_assert(IsStdLayout<astl_get_system_info_params_t>());
static_assert(offsetof(astl_get_system_info_params_t, size) == 0);
static_assert(offsetof(astl_get_system_info_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_system_info_params_t, system_info) == kFirstPtrOffset);
static_assert(sizeof(astl_get_system_info_params_t) == StructLayoutSize<size_t, uint32_t, astl_platform_props_t*>());

static_assert(IsStdLayout<astl_get_target_count_params_t>());
static_assert(offsetof(astl_get_target_count_params_t, size) == 0);
static_assert(offsetof(astl_get_target_count_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_target_count_params_t, target_count) == kFirstPtrOffset);
static_assert(sizeof(astl_get_target_count_params_t) == StructLayoutSize<size_t, uint32_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_targets_params_t>());
static_assert(offsetof(astl_get_targets_params_t, size) == 0);
static_assert(offsetof(astl_get_targets_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_targets_params_t, targets) == kFirstPtrOffset);
static_assert(sizeof(astl_get_targets_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_props_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_counter_count_params_t>());
static_assert(offsetof(astl_get_counter_count_params_t, size) == 0);
static_assert(offsetof(astl_get_counter_count_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_counter_count_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_counter_count_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_counters_params_t>());
static_assert(offsetof(astl_get_counters_params_t, size) == 0);
static_assert(offsetof(astl_get_counters_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_counters_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_counters_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_counter_props_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_count_params_t>());
static_assert(offsetof(astl_get_metric_count_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_count_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_count_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_count_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_metrics_params_t>());
static_assert(offsetof(astl_get_metrics_params_t, size) == 0);
static_assert(offsetof(astl_get_metrics_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metrics_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metrics_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_props_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_state_count_on_target_params_t>());
static_assert(offsetof(astl_get_metric_state_count_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_state_count_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_state_count_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_state_count_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_states_on_target_params_t>());
static_assert(offsetof(astl_get_metric_states_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_states_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_states_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(
    sizeof(astl_get_metric_states_on_target_params_t) ==
    StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t, astl_state_props_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_group_count_params_t>());
static_assert(offsetof(astl_get_metric_group_count_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_group_count_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_group_count_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_group_count_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_groups_params_t>());
static_assert(offsetof(astl_get_metric_groups_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_groups_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_groups_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_groups_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_group_props_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_group_metrics_params_t>());
static_assert(offsetof(astl_get_metric_group_metrics_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_group_metrics_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_group_metrics_params_t, target_handle) == kFirstPtrOffset);
static_assert(
    sizeof(astl_get_metric_group_metrics_params_t) ==
    StructLayoutSize<size_t, uint32_t, astl_target_handle_t, const astl_metric_group_props_t*, astl_metric_props_t*>());

static_assert(IsStdLayout<astl_configure_counter_collection_on_target_params_t>());
static_assert(offsetof(astl_configure_counter_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_configure_counter_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_configure_counter_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_configure_counter_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, const astl_collection_params_t*,
                               const astl_counter_handle_t*, uint32_t>());

static_assert(IsStdLayout<astl_configure_counter_collection_params_t>());
static_assert(offsetof(astl_configure_counter_collection_params_t, size) == 0);
static_assert(offsetof(astl_configure_counter_collection_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_configure_counter_collection_params_t, collection_params) == kFirstPtrOffset);
static_assert(
    sizeof(astl_configure_counter_collection_params_t) ==
    StructLayoutSize<size_t, uint32_t, const astl_collection_params_t*, const astl_counter_handle_t*, uint32_t>());

static_assert(IsStdLayout<astl_configure_metric_collection_on_target_params_t>());
static_assert(offsetof(astl_configure_metric_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_configure_metric_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_configure_metric_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_configure_metric_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, const astl_collection_params_t*,
                               const astl_metric_handle_t*, uint32_t>());

static_assert(IsStdLayout<astl_configure_metric_collection_params_t>());
static_assert(offsetof(astl_configure_metric_collection_params_t, size) == 0);
static_assert(offsetof(astl_configure_metric_collection_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_configure_metric_collection_params_t, collection_params) == kFirstPtrOffset);
static_assert(
    sizeof(astl_configure_metric_collection_params_t) ==
    StructLayoutSize<size_t, uint32_t, const astl_collection_params_t*, const astl_metric_handle_t*, uint32_t>());

static_assert(IsStdLayout<astl_configure_metric_group_collection_on_target_params_t>());
static_assert(offsetof(astl_configure_metric_group_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_configure_metric_group_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_configure_metric_group_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_configure_metric_group_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, const astl_collection_params_t*,
                               const astl_metric_group_handle_t*, uint32_t>());

static_assert(IsStdLayout<astl_configure_metric_group_collection_params_t>());
static_assert(offsetof(astl_configure_metric_group_collection_params_t, size) == 0);
static_assert(offsetof(astl_configure_metric_group_collection_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_configure_metric_group_collection_params_t, collection_params) == kFirstPtrOffset);
static_assert(
    sizeof(astl_configure_metric_group_collection_params_t) ==
    StructLayoutSize<size_t, uint32_t, const astl_collection_params_t*, const astl_metric_group_handle_t*, uint32_t>());

static_assert(IsStdLayout<astl_read_immediate_on_target_params_t>());
static_assert(offsetof(astl_read_immediate_on_target_params_t, size) == 0);
static_assert(offsetof(astl_read_immediate_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_read_immediate_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_read_immediate_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t>());

static_assert(IsStdLayout<astl_read_immediate_params_t>());
static_assert(offsetof(astl_read_immediate_params_t, size) == 0);
static_assert(offsetof(astl_read_immediate_params_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_read_immediate_params_t) == StructLayoutSize<size_t, uint32_t>());

static_assert(IsStdLayout<astl_start_collection_on_target_params_t>());
static_assert(offsetof(astl_start_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_start_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_start_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_start_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t>());

static_assert(IsStdLayout<astl_start_collection_params_t>());
static_assert(offsetof(astl_start_collection_params_t, size) == 0);
static_assert(offsetof(astl_start_collection_params_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_start_collection_params_t) == StructLayoutSize<size_t, uint32_t>());

static_assert(IsStdLayout<astl_pause_collection_on_target_params_t>());
static_assert(offsetof(astl_pause_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_pause_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_pause_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_pause_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t>());

static_assert(IsStdLayout<astl_pause_collection_params_t>());
static_assert(offsetof(astl_pause_collection_params_t, size) == 0);
static_assert(offsetof(astl_pause_collection_params_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_pause_collection_params_t) == StructLayoutSize<size_t, uint32_t>());

static_assert(IsStdLayout<astl_resume_collection_on_target_params_t>());
static_assert(offsetof(astl_resume_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_resume_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_resume_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_resume_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t>());

static_assert(IsStdLayout<astl_resume_collection_params_t>());
static_assert(offsetof(astl_resume_collection_params_t, size) == 0);
static_assert(offsetof(astl_resume_collection_params_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_resume_collection_params_t) == StructLayoutSize<size_t, uint32_t>());

static_assert(IsStdLayout<astl_stop_collection_on_target_params_t>());
static_assert(offsetof(astl_stop_collection_on_target_params_t, size) == 0);
static_assert(offsetof(astl_stop_collection_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_stop_collection_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_stop_collection_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t>());

static_assert(IsStdLayout<astl_stop_collection_params_t>());
static_assert(offsetof(astl_stop_collection_params_t, size) == 0);
static_assert(offsetof(astl_stop_collection_params_t, flags) == kFlagsOffset);
static_assert(sizeof(astl_stop_collection_params_t) == StructLayoutSize<size_t, uint32_t>());

static_assert(IsStdLayout<astl_get_counter_sample_count_on_target_params_t>());
static_assert(offsetof(astl_get_counter_sample_count_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_counter_sample_count_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_counter_sample_count_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_counter_sample_count_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_counter_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_counter_samples_on_target_params_t>());
static_assert(offsetof(astl_get_counter_samples_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_counter_samples_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_counter_samples_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(
    sizeof(astl_get_counter_samples_on_target_params_t) ==
    StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_counter_handle_t, astl_sample_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_sample_count_on_target_params_t>());
static_assert(offsetof(astl_get_metric_sample_count_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_sample_count_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_sample_count_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_sample_count_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_samples_on_target_params_t>());
static_assert(offsetof(astl_get_metric_samples_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_samples_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_samples_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(
    sizeof(astl_get_metric_samples_on_target_params_t) ==
    StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t, astl_sample_t*, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_statistics_on_target_params_t>());
static_assert(offsetof(astl_get_metric_statistics_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_statistics_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_statistics_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(
    sizeof(astl_get_metric_statistics_on_target_params_t) ==
    StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t, astl_metric_statistics_t*>());

static_assert(IsStdLayout<astl_get_metric_discrete_histogram_bin_count_on_target_params_t>());
static_assert(offsetof(astl_get_metric_discrete_histogram_bin_count_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_discrete_histogram_bin_count_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_discrete_histogram_bin_count_on_target_params_t, target_handle) ==
              kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_discrete_histogram_bin_count_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t, uint32_t*>());

static_assert(IsStdLayout<astl_get_metric_discrete_histogram_on_target_params_t>());
static_assert(offsetof(astl_get_metric_discrete_histogram_on_target_params_t, size) == 0);
static_assert(offsetof(astl_get_metric_discrete_histogram_on_target_params_t, flags) == kFlagsOffset);
static_assert(offsetof(astl_get_metric_discrete_histogram_on_target_params_t, target_handle) == kFirstPtrOffset);
static_assert(sizeof(astl_get_metric_discrete_histogram_on_target_params_t) ==
              StructLayoutSize<size_t, uint32_t, astl_target_handle_t, astl_metric_handle_t,
                               astl_discrete_histogram_bin_t*, uint32_t*>());

}  // namespace

TEST_CASE("Public C API struct layout guards compile", "[unit][api][abi]") { SUCCEED(); }
