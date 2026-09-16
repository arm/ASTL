// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_utils.hpp"
#include "fixtures/minimal_procfs_fixture.hpp"
#include "orchestrator/orchestrator.hpp"

namespace {

constexpr size_t   kMaxOperations            = 32;
constexpr size_t   kMaxItems                 = 256;
constexpr uint32_t kBitsPerByte              = 8;
constexpr uint8_t  kStructChoiceCount        = 10;
constexpr uint8_t  kValidStructChoiceCount   = 7;
constexpr uint8_t  kUndersizedStructChoice   = 7;
constexpr uint8_t  kNullArgumentFrequency    = 10;
constexpr uint8_t  kInvalidArgumentFrequency = 5;

enum class Operation : uint8_t {
  SYSTEM_INFO,
  TARGETS,
  COUNTERS,
  METRICS,
  METRIC_STATES,
  GLOBAL_METRIC_GROUPS,
  TARGET_METRIC_GROUPS,
  GLOBAL_GROUP_METRICS,
  TARGET_GROUP_METRICS,
  REPEAT_DISCOVERY,
  COUNT,
};

class ByteCursor {
 public:
  ByteCursor(const uint8_t* data, size_t size) : data_{data, size} {}

  auto Take() -> uint8_t { return offset_ < data_.size() ? data_[offset_++] : 0; }

  auto TakeUint32() -> uint32_t {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
      value |= static_cast<uint32_t>(Take()) << (index * kBitsPerByte);
    }
    return value;
  }

  auto Index(size_t size) -> size_t { return size == 0 ? 0 : static_cast<size_t>(TakeUint32()) % size; }

 private:
  std::span<const uint8_t> data_;
  size_t                   offset_{0};
};

[[noreturn]] auto Fail() -> void { __builtin_trap(); }

auto CheckStatus(astl_status_code status) -> void {
  if (status < ASTL_STATUS_SUCCESS || status > ASTL_STATUS_INTERNAL_ERROR) {
    Fail();
  }
}

auto Require(astl_status_code status, astl_status_code expected = ASTL_STATUS_SUCCESS) -> void {
  CheckStatus(status);
  if (status != expected) {
    Fail();
  }
}

template <typename T>
auto StructSize(ByteCursor& input) -> size_t {
  const auto choice = input.Take() % kStructChoiceCount;
  if (choice < kValidStructChoiceCount) {
    return sizeof(T);
  }
  if (choice == kUndersizedStructChoice) {
    return 0;
  }
  return choice == kUndersizedStructChoice + 1U ? sizeof(T) - 1U : sizeof(T) + 1U;
}

auto SameString(const char* left, const char* right) -> bool {
  return left == nullptr || right == nullptr ? left == right : std::strcmp(left, right) == 0;
}

auto SameTarget(const astl_target_props_t& left, const astl_target_props_t& right) -> bool {
  return left.handle == right.handle && left.parent_handle == right.parent_handle &&
         SameString(left.name, right.name) && SameString(left.description, right.description) &&
         SameString(left.id, right.id);
}

auto SameCounter(const astl_counter_props_t& left, const astl_counter_props_t& right) -> bool {
  return left.handle == right.handle && SameString(left.name, right.name) &&
         SameString(left.description, right.description) && left.min_sampling_interval == right.min_sampling_interval &&
         left.units == right.units && SameString(left.formula, right.formula) && left.value_type == right.value_type &&
         left.counter_type == right.counter_type;
}

auto SameMetric(const astl_metric_props_t& left, const astl_metric_props_t& right) -> bool {
  return left.handle == right.handle && SameString(left.name, right.name) &&
         SameString(left.description, right.description) && left.min_sampling_interval == right.min_sampling_interval &&
         left.units == right.units && left.value_type == right.value_type && left.metric_type == right.metric_type &&
         left.identifier == right.identifier;
}

auto SameState(const astl_state_props_t& left, const astl_state_props_t& right) -> bool {
  return SameString(left.name, right.name) && SameString(left.description, right.description) &&
         std::memcmp(&left.value, &right.value, sizeof(left.value)) == 0;
}

auto SameGroup(const astl_metric_group_props_t& left, const astl_metric_group_props_t& right) -> bool {
  return left.handle == right.handle && SameString(left.name, right.name) &&
         SameString(left.description, right.description);
}

template <typename T>
auto InitializeOutput(std::vector<T>& output) -> void {
  if (!output.empty()) {
    output.front().size = sizeof(T);
  }
}

auto CheckedSize(uint32_t count) -> size_t {
  if (count > kMaxItems) {
    Fail();
  }
  return count;
}

struct TargetSnapshot {
  astl_target_props_t                           properties{};
  std::vector<astl_counter_props_t>             counters;
  std::vector<astl_metric_props_t>              metrics;
  std::vector<std::vector<astl_state_props_t>>  states;
  std::vector<astl_metric_group_props_t>        groups;
  std::vector<std::vector<astl_metric_props_t>> group_metrics;
};

struct TopologySnapshot {
  astl_platform_props_t                         system_info{};
  std::vector<TargetSnapshot>                   targets;
  std::vector<astl_metric_group_props_t>        groups;
  std::vector<std::vector<astl_metric_props_t>> group_metrics;
};

auto GetSystemInfo() -> astl_platform_props_t {
  astl_platform_props_t info{};
  info.size  = sizeof(astl_platform_props_t);
  info.flags = ASTL_SYSTEM_INFO_FLAG_HOST;
  astl_get_system_info_params_t params{.size = sizeof(astl_get_system_info_params_t), .flags = 0, .system_info = &info};
  Require(astlGetSystemInfo(&params));
  return info;
}

auto GetTargets() -> std::vector<astl_target_props_t> {
  uint32_t                       count = 0;
  astl_get_target_count_params_t count_params{
      .size = sizeof(astl_get_target_count_params_t), .flags = 0, .target_count = &count};
  Require(astlGetTargetCount(&count_params));
  CheckedSize(count);
  if (count == 0) {
    Fail();
  }
  std::vector<astl_target_props_t> output(count);
  InitializeOutput(output);
  astl_get_targets_params_t params{
      .size = sizeof(astl_get_targets_params_t), .flags = 0, .targets = output.data(), .target_count = &count};
  Require(astlGetTargets(&params));
  return output;
}

auto GetCounters(astl_target_handle_t target) -> std::vector<astl_counter_props_t> {
  uint32_t                        count = 0;
  astl_get_counter_count_params_t count_params{
      .size = sizeof(astl_get_counter_count_params_t), .flags = 0, .target_handle = target, .counter_count = &count};
  Require(astlGetCounterCountOnTarget(&count_params));
  CheckedSize(count);
  if (count == 0) {
    return {};
  }
  std::vector<astl_counter_props_t> output(count);
  InitializeOutput(output);
  astl_get_counters_params_t params{.size          = sizeof(astl_get_counters_params_t),
                                    .flags         = 0,
                                    .target_handle = target,
                                    .counters      = output.data(),
                                    .counter_count = &count};
  Require(astlGetCountersOnTarget(&params));
  return output;
}

auto GetMetrics(astl_target_handle_t target) -> std::vector<astl_metric_props_t> {
  uint32_t                       count = 0;
  astl_get_metric_count_params_t count_params{
      .size = sizeof(astl_get_metric_count_params_t), .flags = 0, .target_handle = target, .metric_count = &count};
  Require(astlGetMetricCountOnTarget(&count_params));
  CheckedSize(count);
  if (count == 0) {
    return {};
  }
  std::vector<astl_metric_props_t> output(count);
  InitializeOutput(output);
  astl_get_metrics_params_t params{.size          = sizeof(astl_get_metrics_params_t),
                                   .flags         = 0,
                                   .target_handle = target,
                                   .metrics       = output.data(),
                                   .metric_count  = &count};
  Require(astlGetMetricsOnTarget(&params));
  return output;
}

auto IsStateMetric(const astl_metric_props_t& metric) -> bool {
  return metric.metric_type == ASTL_METRIC_FINITE_SET_VALUE || metric.metric_type == ASTL_METRIC_RESIDENCY;
}

auto GetStates(astl_target_handle_t target, const astl_metric_props_t& metric) -> std::vector<astl_state_props_t> {
  if (!IsStateMetric(metric)) {
    return {};
  }
  uint32_t                                       count = 0;
  astl_get_metric_state_count_on_target_params_t count_params{
      .size          = sizeof(astl_get_metric_state_count_on_target_params_t),
      .flags         = 0,
      .target_handle = target,
      .metric_handle = metric.handle,
      .state_count   = &count};
  Require(astlGetMetricStateCountOnTarget(&count_params));
  CheckedSize(count);
  if (count == 0) {
    return {};
  }
  std::vector<astl_state_props_t> output(count);
  InitializeOutput(output);
  astl_get_metric_states_on_target_params_t params{.size          = sizeof(astl_get_metric_states_on_target_params_t),
                                                   .flags         = 0,
                                                   .target_handle = target,
                                                   .metric_handle = metric.handle,
                                                   .states        = output.data(),
                                                   .state_count   = &count};
  Require(astlGetMetricStatesOnTarget(&params));
  return output;
}

auto GetGroups(astl_target_handle_t target = nullptr) -> std::vector<astl_metric_group_props_t> {
  uint32_t         count = 0;
  astl_status_code status{};
  if (target == nullptr) {
    astl_get_metric_group_count_params_t params{
        .size = sizeof(astl_get_metric_group_count_params_t), .flags = 0, .metric_group_count = &count};
    status = astlGetMetricGroupCount(&params);
  } else {
    astl_get_metric_group_count_on_target_params_t params{
        .size               = sizeof(astl_get_metric_group_count_on_target_params_t),
        .flags              = 0,
        .target_handle      = target,
        .metric_group_count = &count};
    status = astlGetMetricGroupCountOnTarget(&params);
  }
  Require(status);
  CheckedSize(count);
  if (count == 0) {
    return {};
  }
  std::vector<astl_metric_group_props_t> output(count);
  InitializeOutput(output);
  if (target == nullptr) {
    astl_get_metric_groups_params_t params{.size               = sizeof(astl_get_metric_groups_params_t),
                                           .flags              = 0,
                                           .metric_groups      = output.data(),
                                           .metric_group_count = &count};
    status = astlGetMetricGroups(&params);
  } else {
    astl_get_metric_groups_on_target_params_t params{.size          = sizeof(astl_get_metric_groups_on_target_params_t),
                                                     .flags         = 0,
                                                     .target_handle = target,
                                                     .metric_groups = output.data(),
                                                     .metric_group_count = &count};
    status = astlGetMetricGroupsOnTarget(&params);
  }
  Require(status);
  return output;
}

auto GetGroupMetrics(astl_metric_group_handle_t group, astl_target_handle_t target = nullptr)
    -> std::vector<astl_metric_props_t> {
  uint32_t         count = 0;
  astl_status_code status{};
  if (target == nullptr) {
    astl_get_metric_group_metric_count_params_t params{.size  = sizeof(astl_get_metric_group_metric_count_params_t),
                                                       .flags = 0,
                                                       .metric_group_handle = group,
                                                       .metric_count        = &count};
    status = astlGetMetricGroupMetricCount(&params);
  } else {
    astl_get_metric_group_metric_count_on_target_params_t params{
        .size                = sizeof(astl_get_metric_group_metric_count_on_target_params_t),
        .flags               = 0,
        .target_handle       = target,
        .metric_group_handle = group,
        .metric_count        = &count};
    status = astlGetMetricGroupMetricCountOnTarget(&params);
  }
  Require(status);
  CheckedSize(count);
  if (count == 0) {
    return {};
  }
  std::vector<astl_metric_props_t> output(count);
  InitializeOutput(output);
  if (target == nullptr) {
    astl_get_metric_group_metrics_params_t params{.size                = sizeof(astl_get_metric_group_metrics_params_t),
                                                  .flags               = 0,
                                                  .metric_group_handle = group,
                                                  .metrics             = output.data(),
                                                  .metric_count        = &count};
    status = astlGetMetricGroupMetrics(&params);
  } else {
    astl_get_metric_group_metrics_on_target_params_t params{
        .size                = sizeof(astl_get_metric_group_metrics_on_target_params_t),
        .flags               = 0,
        .target_handle       = target,
        .metric_group_handle = group,
        .metrics             = output.data(),
        .metric_count        = &count};
    status = astlGetMetricGroupMetricsOnTarget(&params);
  }
  Require(status);
  return output;
}

auto CaptureTopology() -> TopologySnapshot {
  TopologySnapshot snapshot;
  snapshot.system_info = GetSystemInfo();
  const auto targets   = GetTargets();
  snapshot.targets.reserve(targets.size());
  for (const auto& properties : targets) {
    TargetSnapshot target;
    target.properties = properties;
    target.counters   = GetCounters(properties.handle);
    target.metrics    = GetMetrics(properties.handle);
    target.states.reserve(target.metrics.size());
    for (const auto& metric : target.metrics) {
      target.states.push_back(GetStates(properties.handle, metric));
    }
    target.groups = GetGroups(properties.handle);
    target.group_metrics.reserve(target.groups.size());
    for (const auto& group : target.groups) {
      target.group_metrics.push_back(GetGroupMetrics(group.handle, properties.handle));
    }
    snapshot.targets.push_back(std::move(target));
  }
  snapshot.groups = GetGroups();
  snapshot.group_metrics.reserve(snapshot.groups.size());
  for (const auto& group : snapshot.groups) {
    snapshot.group_metrics.push_back(GetGroupMetrics(group.handle));
  }
  return snapshot;
}

template <typename T, typename Compare>
auto CheckElements(const std::vector<T>& actual, const std::vector<T>& expected, Compare compare) -> void {
  if (actual.size() != expected.size()) {
    Fail();
  }
  for (size_t index = 0; index < actual.size(); ++index) {
    if (!compare(actual[index], expected[index])) {
      Fail();
    }
  }
}

auto SamePlatform(const astl_platform_props_t& left, const astl_platform_props_t& right) -> bool {
  return left.flags == right.flags && SameString(left.soc_name, right.soc_name) &&
         SameString(left.vendor_id, right.vendor_id) && SameString(left.os_name, right.os_name) &&
         SameString(left.kernel_name, right.kernel_name) && SameString(left.kernel_version, right.kernel_version) &&
         SameString(left.kernel_release, right.kernel_release) &&
         SameString(left.firmware_version, right.firmware_version) && SameString(left.hostname, right.hostname) &&
         SameString(left.architecture, right.architecture) && SameString(left.cpu_type, right.cpu_type) &&
         SameString(left.cpu_features, right.cpu_features) && SameString(left.cache_info, right.cache_info) &&
         left.core_count == right.core_count && left.numa_node_count == right.numa_node_count &&
         left.socket_count == right.socket_count && left.cache_line_size_bytes == right.cache_line_size_bytes &&
         left.memory_total_bytes == right.memory_total_bytes && SameString(left.libc_version, right.libc_version) &&
         SameString(left.boot_info, right.boot_info) && left.huge_pages_total == right.huge_pages_total &&
         left.huge_page_size_kb == right.huge_page_size_kb &&
         SameString(left.transparent_huge_pages, right.transparent_huge_pages);
}

auto CheckTopology(const TopologySnapshot& actual, const TopologySnapshot& expected) -> void {
  if (!SamePlatform(actual.system_info, expected.system_info) || actual.targets.size() != expected.targets.size()) {
    Fail();
  }
  for (size_t index = 0; index < actual.targets.size(); ++index) {
    const auto& left  = actual.targets[index];
    const auto& right = expected.targets[index];
    if (!SameTarget(left.properties, right.properties)) {
      Fail();
    }
    CheckElements(left.counters, right.counters, SameCounter);
    CheckElements(left.metrics, right.metrics, SameMetric);
    CheckElements(left.groups, right.groups, SameGroup);
    if (left.states.size() != right.states.size() || left.group_metrics.size() != right.group_metrics.size()) {
      Fail();
    }
    for (size_t metric = 0; metric < left.states.size(); ++metric) {
      CheckElements(left.states[metric], right.states[metric], SameState);
    }
    for (size_t group = 0; group < left.group_metrics.size(); ++group) {
      CheckElements(left.group_metrics[group], right.group_metrics[group], SameMetric);
    }
  }
  CheckElements(actual.groups, expected.groups, SameGroup);
  if (actual.group_metrics.size() != expected.group_metrics.size()) {
    Fail();
  }
  for (size_t group = 0; group < actual.group_metrics.size(); ++group) {
    CheckElements(actual.group_metrics[group], expected.group_metrics[group], SameMetric);
  }
}

auto Capacity(ByteCursor& input, size_t required) -> uint32_t {
  const std::array<size_t, 5> choices{0, 1, required == 0 ? 0 : required - 1, required,
                                      std::min(required + 1, kMaxItems + 1)};
  return static_cast<uint32_t>(choices.at(input.Take() % choices.size()));
}

template <typename T, typename Compare>
auto ValidateGetter(astl_status_code status, uint32_t capacity, uint32_t count, size_t required,
                    const std::vector<T>& output, const std::vector<T>& expected, Compare compare,
                    bool canonical_arguments) -> void {
  CheckStatus(status);
  if (!canonical_arguments) {
    return;
  }
  if (capacity == 0) {
    if (status != ASTL_STATUS_BAD_ARGUMENT) {
      Fail();
    }
    return;
  }
  if (capacity < required) {
    if (status != ASTL_STATUS_BUFFER_TOO_SMALL || count != required) {
      Fail();
    }
    return;
  }
  if (required == 0) {
    return;
  }
  const bool expected_status =
      status == ASTL_STATUS_SUCCESS || (capacity > required && status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  if (!expected_status || count != required || count > capacity) {
    Fail();
  }
  for (size_t index = 0; index < required; ++index) {
    if (!compare(output[index], expected[index])) {
      Fail();
    }
  }
}

struct ArgumentChoices {
  bool valid_params;
  bool valid_flags;
  bool valid_output;
  bool valid_count;
  bool valid_element;
  bool valid_handle;
};

auto ChooseArguments(ByteCursor& input) -> ArgumentChoices {
  return ArgumentChoices{
      .valid_params  = input.Take() % kNullArgumentFrequency != 0U,
      .valid_flags   = input.Take() % kInvalidArgumentFrequency != 0U,
      .valid_output  = input.Take() % kNullArgumentFrequency != 0U,
      .valid_count   = input.Take() % kNullArgumentFrequency != 0U,
      .valid_element = input.Take() % kInvalidArgumentFrequency != 0U,
      .valid_handle  = input.Take() % kInvalidArgumentFrequency != 0U,
  };
}

template <typename T>
auto ChosenSize(ByteCursor& input, bool valid) -> size_t {
  if (valid) {
    return sizeof(T);
  }
  const auto size = StructSize<T>(input);
  return size == sizeof(T) ? 0 : size;
}

auto InvalidOpaqueHandle(const TopologySnapshot& topology) -> const void* {
  for (const auto& target : topology.targets) {
    if (!target.metrics.empty()) {
      return target.metrics.front().handle;
    }
  }
  return nullptr;
}

auto TargetHandle(ByteCursor& input, const TopologySnapshot& topology, size_t target_index, bool valid)
    -> astl_target_handle_t {
  if (valid) {
    return topology.targets[target_index].properties.handle;
  }
  return input.Take() % 2U == 0U ? nullptr : static_cast<astl_target_handle_t>(InvalidOpaqueHandle(topology));
}

template <typename T, typename Compare, typename Call>
auto FuzzOutput(ByteCursor& input, const std::vector<T>& expected, size_t count_query_result, ArgumentChoices choices,
                Call call, Compare compare) -> void {
  const auto     capacity = Capacity(input, count_query_result);
  std::vector<T> output(std::max<size_t>(1, capacity));
  output.front().size  = ChosenSize<T>(input, choices.valid_element);
  uint32_t   count     = capacity;
  const auto status    = call(choices.valid_output ? output.data() : nullptr, choices.valid_count ? &count : nullptr);
  const bool canonical = choices.valid_params && choices.valid_flags && choices.valid_output && choices.valid_count &&
                         choices.valid_element && choices.valid_handle;
  ValidateGetter(status, capacity, count, expected.size(), output, expected, compare, canonical);
}

auto FuzzSystemInfo(ByteCursor& input, const TopologySnapshot& topology) -> void {
  const auto            choices = ChooseArguments(input);
  astl_platform_props_t output{};
  output.size  = ChosenSize<astl_platform_props_t>(input, choices.valid_element);
  output.flags = input.Take() % 4U == 0U ? UINT32_MAX : ASTL_SYSTEM_INFO_FLAG_HOST;
  astl_get_system_info_params_t params{.size  = ChosenSize<astl_get_system_info_params_t>(input, choices.valid_params),
                                       .flags = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
                                       .system_info = choices.valid_output ? &output : nullptr};
  const auto                    status = astlGetSystemInfo(choices.valid_count ? &params : nullptr);
  CheckStatus(status);
  if (status == ASTL_STATUS_SUCCESS && choices.valid_params && choices.valid_flags && choices.valid_output &&
      choices.valid_count && choices.valid_element && output.flags == ASTL_SYSTEM_INFO_FLAG_HOST &&
      !SamePlatform(output, topology.system_info)) {
    Fail();
  }
}

auto FuzzTargets(ByteCursor& input, const TopologySnapshot& topology) -> void {
  const auto                     count_choices = ChooseArguments(input);
  uint32_t                       count         = input.TakeUint32();
  astl_get_target_count_params_t count_params{
      .size         = ChosenSize<astl_get_target_count_params_t>(input, count_choices.valid_params),
      .flags        = count_choices.valid_flags ? 0U : input.TakeUint32() | 1U,
      .target_count = count_choices.valid_count ? &count : nullptr};
  const auto count_status = astlGetTargetCount(count_choices.valid_output ? &count_params : nullptr);
  CheckStatus(count_status);
  if (count_status == ASTL_STATUS_SUCCESS && count_choices.valid_params && count_choices.valid_flags &&
      count_choices.valid_count && count_choices.valid_output && count != topology.targets.size()) {
    Fail();
  }

  std::vector<astl_target_props_t> expected;
  expected.reserve(topology.targets.size());
  for (const auto& target : topology.targets) {
    expected.push_back(target.properties);
  }
  auto choices = ChooseArguments(input);
  FuzzOutput(
      input, expected, count_status == ASTL_STATUS_SUCCESS ? count : expected.size(), choices,
      [&](astl_target_props_t* output, uint32_t* output_count) {
        astl_get_targets_params_t params{.size    = ChosenSize<astl_get_targets_params_t>(input, choices.valid_params),
                                         .flags   = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
                                         .targets = output,
                                         .target_count = output_count};
        return astlGetTargets(choices.valid_handle ? &params : nullptr);
      },
      SameTarget);
}

auto FuzzCounters(ByteCursor& input, const TopologySnapshot& topology) -> void {
  const auto                      target_index = input.Index(topology.targets.size());
  const auto&                     target       = topology.targets[target_index];
  auto                            choices      = ChooseArguments(input);
  const astl_target_handle_t      handle       = TargetHandle(input, topology, target_index, choices.valid_handle);
  uint32_t                        count        = input.TakeUint32();
  astl_get_counter_count_params_t count_params{
      .size          = ChosenSize<astl_get_counter_count_params_t>(input, choices.valid_params),
      .flags         = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
      .target_handle = handle,
      .counter_count = choices.valid_count ? &count : nullptr};
  const auto count_status = astlGetCounterCountOnTarget(choices.valid_output ? &count_params : nullptr);
  CheckStatus(count_status);
  if (count_status == ASTL_STATUS_SUCCESS && choices.valid_params && choices.valid_flags && choices.valid_count &&
      choices.valid_output && choices.valid_handle && count != target.counters.size()) {
    Fail();
  }

  choices                                  = ChooseArguments(input);
  const astl_target_handle_t getter_handle = TargetHandle(input, topology, target_index, choices.valid_handle);
  FuzzOutput(
      input, target.counters, count_status == ASTL_STATUS_SUCCESS ? count : target.counters.size(), choices,
      [&](astl_counter_props_t* output, uint32_t* output_count) {
        astl_get_counters_params_t params{.size  = ChosenSize<astl_get_counters_params_t>(input, choices.valid_params),
                                          .flags = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
                                          .target_handle = getter_handle,
                                          .counters      = output,
                                          .counter_count = output_count};
        return astlGetCountersOnTarget(choices.valid_output ? &params : nullptr);
      },
      SameCounter);
}

auto FuzzMetrics(ByteCursor& input, const TopologySnapshot& topology) -> void {
  const auto                     target_index = input.Index(topology.targets.size());
  const auto&                    target       = topology.targets[target_index];
  auto                           choices      = ChooseArguments(input);
  const astl_target_handle_t     handle       = TargetHandle(input, topology, target_index, choices.valid_handle);
  uint32_t                       count        = input.TakeUint32();
  astl_get_metric_count_params_t count_params{
      .size          = ChosenSize<astl_get_metric_count_params_t>(input, choices.valid_params),
      .flags         = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
      .target_handle = handle,
      .metric_count  = choices.valid_count ? &count : nullptr};
  const auto count_status = astlGetMetricCountOnTarget(choices.valid_output ? &count_params : nullptr);
  CheckStatus(count_status);
  if (count_status == ASTL_STATUS_SUCCESS && choices.valid_params && choices.valid_flags && choices.valid_count &&
      choices.valid_output && choices.valid_handle && count != target.metrics.size()) {
    Fail();
  }
  choices                                  = ChooseArguments(input);
  const astl_target_handle_t getter_handle = TargetHandle(input, topology, target_index, choices.valid_handle);
  FuzzOutput(
      input, target.metrics, count_status == ASTL_STATUS_SUCCESS ? count : target.metrics.size(), choices,
      [&](astl_metric_props_t* output, uint32_t* output_count) {
        astl_get_metrics_params_t params{.size  = ChosenSize<astl_get_metrics_params_t>(input, choices.valid_params),
                                         .flags = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
                                         .target_handle = getter_handle,
                                         .metrics       = output,
                                         .metric_count  = output_count};
        return astlGetMetricsOnTarget(choices.valid_output ? &params : nullptr);
      },
      SameMetric);
}

auto ChooseMetric(ByteCursor& input, const TopologySnapshot& topology) -> std::pair<size_t, size_t> {
  std::vector<std::pair<size_t, size_t>> choices;
  for (size_t target = 0; target < topology.targets.size(); ++target) {
    for (size_t metric = 0; metric < topology.targets[target].metrics.size(); ++metric) {
      choices.emplace_back(target, metric);
    }
  }
  return choices.empty() ? std::pair<size_t, size_t>{0, 0} : choices[input.Index(choices.size())];
}

auto FuzzMetricStates(ByteCursor& input, const TopologySnapshot& topology) -> void {
  const auto [target_index, metric_index] = ChooseMetric(input, topology);
  const auto& target                      = topology.targets[target_index];
  if (target.metrics.empty()) {
    return;
  }
  const auto&          metric        = target.metrics[metric_index];
  const auto&          expected      = target.states[metric_index];
  auto                 choices       = ChooseArguments(input);
  astl_target_handle_t target_handle = TargetHandle(input, topology, target_index, choices.valid_handle);
  astl_metric_handle_t metric_handle =
      choices.valid_handle ? metric.handle : static_cast<astl_metric_handle_t>(target.properties.handle);
  uint32_t                                       count = input.TakeUint32();
  astl_get_metric_state_count_on_target_params_t count_params{
      .size          = ChosenSize<astl_get_metric_state_count_on_target_params_t>(input, choices.valid_params),
      .flags         = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
      .target_handle = target_handle,
      .metric_handle = metric_handle,
      .state_count   = choices.valid_count ? &count : nullptr};
  const auto count_status = astlGetMetricStateCountOnTarget(choices.valid_output ? &count_params : nullptr);
  CheckStatus(count_status);
  if (IsStateMetric(metric) && count_status == ASTL_STATUS_SUCCESS && choices.valid_params && choices.valid_flags &&
      choices.valid_count && choices.valid_output && choices.valid_handle && count != expected.size()) {
    Fail();
  }
  choices       = ChooseArguments(input);
  target_handle = TargetHandle(input, topology, target_index, choices.valid_handle);
  metric_handle = choices.valid_handle ? metric.handle : static_cast<astl_metric_handle_t>(target.properties.handle);
  FuzzOutput(
      input, expected, count_status == ASTL_STATUS_SUCCESS ? count : expected.size(), choices,
      [&](astl_state_props_t* output, uint32_t* output_count) {
        astl_get_metric_states_on_target_params_t params{
            .size          = ChosenSize<astl_get_metric_states_on_target_params_t>(input, choices.valid_params),
            .flags         = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
            .target_handle = target_handle,
            .metric_handle = metric_handle,
            .states        = output,
            .state_count   = output_count};
        return astlGetMetricStatesOnTarget(choices.valid_output ? &params : nullptr);
      },
      SameState);
}

// Pairing the global and on-target forms keeps their generated argument policies identical.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto FuzzGroups(ByteCursor& input, const TopologySnapshot& topology, bool on_target) -> void {
  const auto                 target_index  = input.Index(topology.targets.size());
  const auto&                expected      = on_target ? topology.targets[target_index].groups : topology.groups;
  auto                       choices       = ChooseArguments(input);
  const astl_target_handle_t target_handle = TargetHandle(input, topology, target_index, choices.valid_handle);
  uint32_t                   count         = input.TakeUint32();
  astl_status_code           count_status{};
  if (on_target) {
    astl_get_metric_group_count_on_target_params_t params{
        .size               = ChosenSize<astl_get_metric_group_count_on_target_params_t>(input, choices.valid_params),
        .flags              = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
        .target_handle      = target_handle,
        .metric_group_count = choices.valid_count ? &count : nullptr};
    count_status = astlGetMetricGroupCountOnTarget(choices.valid_output ? &params : nullptr);
  } else {
    astl_get_metric_group_count_params_t params{
        .size               = ChosenSize<astl_get_metric_group_count_params_t>(input, choices.valid_params),
        .flags              = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
        .metric_group_count = choices.valid_count ? &count : nullptr};
    count_status = astlGetMetricGroupCount(choices.valid_output ? &params : nullptr);
  }
  CheckStatus(count_status);
  if (count_status == ASTL_STATUS_SUCCESS && choices.valid_params && choices.valid_flags && choices.valid_count &&
      choices.valid_output && (!on_target || choices.valid_handle) && count != expected.size()) {
    Fail();
  }
  choices = ChooseArguments(input);
  FuzzOutput(
      input, expected, count_status == ASTL_STATUS_SUCCESS ? count : expected.size(), choices,
      [&](astl_metric_group_props_t* output, uint32_t* output_count) {
        if (on_target) {
          astl_get_metric_groups_on_target_params_t params{
              .size               = ChosenSize<astl_get_metric_groups_on_target_params_t>(input, choices.valid_params),
              .flags              = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
              .target_handle      = TargetHandle(input, topology, target_index, choices.valid_handle),
              .metric_groups      = output,
              .metric_group_count = output_count};
          return astlGetMetricGroupsOnTarget(choices.valid_output ? &params : nullptr);
        }
        astl_get_metric_groups_params_t params{
            .size               = ChosenSize<astl_get_metric_groups_params_t>(input, choices.valid_params),
            .flags              = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
            .metric_groups      = output,
            .metric_group_count = output_count};
        return astlGetMetricGroups(choices.valid_output ? &params : nullptr);
      },
      SameGroup);
}

// Pairing the global and on-target forms keeps their generated argument policies identical.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto FuzzGroupMetrics(ByteCursor& input, const TopologySnapshot& topology, bool on_target) -> void {
  const auto  target_index = input.Index(topology.targets.size());
  const auto& groups       = on_target ? topology.targets[target_index].groups : topology.groups;
  if (groups.empty()) {
    return;
  }
  const auto  group_index = input.Index(groups.size());
  const auto& expected =
      on_target ? topology.targets[target_index].group_metrics[group_index] : topology.group_metrics[group_index];
  auto                             choices = ChooseArguments(input);
  const astl_metric_group_handle_t group_handle =
      choices.valid_handle ? groups[group_index].handle
                           : static_cast<astl_metric_group_handle_t>(topology.targets[target_index].properties.handle);
  const astl_target_handle_t target_handle = TargetHandle(input, topology, target_index, choices.valid_handle);
  uint32_t                   count         = input.TakeUint32();
  astl_status_code           count_status{};
  if (on_target) {
    astl_get_metric_group_metric_count_on_target_params_t params{
        .size          = ChosenSize<astl_get_metric_group_metric_count_on_target_params_t>(input, choices.valid_params),
        .flags         = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
        .target_handle = target_handle,
        .metric_group_handle = group_handle,
        .metric_count        = choices.valid_count ? &count : nullptr};
    count_status = astlGetMetricGroupMetricCountOnTarget(choices.valid_output ? &params : nullptr);
  } else {
    astl_get_metric_group_metric_count_params_t params{
        .size                = ChosenSize<astl_get_metric_group_metric_count_params_t>(input, choices.valid_params),
        .flags               = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
        .metric_group_handle = group_handle,
        .metric_count        = choices.valid_count ? &count : nullptr};
    count_status = astlGetMetricGroupMetricCount(choices.valid_output ? &params : nullptr);
  }
  CheckStatus(count_status);
  if (count_status == ASTL_STATUS_SUCCESS && choices.valid_params && choices.valid_flags && choices.valid_count &&
      choices.valid_output && choices.valid_handle && count != expected.size()) {
    Fail();
  }
  choices = ChooseArguments(input);
  const astl_metric_group_handle_t getter_group =
      choices.valid_handle ? groups[group_index].handle
                           : static_cast<astl_metric_group_handle_t>(topology.targets[target_index].properties.handle);
  const astl_target_handle_t getter_target = TargetHandle(input, topology, target_index, choices.valid_handle);
  FuzzOutput(
      input, expected, count_status == ASTL_STATUS_SUCCESS ? count : expected.size(), choices,
      [&](astl_metric_props_t* output, uint32_t* output_count) {
        if (on_target) {
          astl_get_metric_group_metrics_on_target_params_t params{
              .size  = ChosenSize<astl_get_metric_group_metrics_on_target_params_t>(input, choices.valid_params),
              .flags = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
              .target_handle       = getter_target,
              .metric_group_handle = getter_group,
              .metrics             = output,
              .metric_count        = output_count};
          return astlGetMetricGroupMetricsOnTarget(choices.valid_output ? &params : nullptr);
        }
        astl_get_metric_group_metrics_params_t params{
            .size                = ChosenSize<astl_get_metric_group_metrics_params_t>(input, choices.valid_params),
            .flags               = choices.valid_flags ? 0U : input.TakeUint32() | 1U,
            .metric_group_handle = getter_group,
            .metrics             = output,
            .metric_count        = output_count};
        return astlGetMetricGroupMetrics(choices.valid_output ? &params : nullptr);
      },
      SameMetric);
}

class DiscoveryFixture {
 public:
  DiscoveryFixture()
      : procfs_{std::filesystem::temp_directory_path() / ("astl-discovery-fuzzer-" + std::to_string(::getpid()))} {
    Require(astl::SetEnvVar(astl::EnvVar::ASTL_PROCFS_ROOT, procfs_.RootPath().string()));
    Require(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "sysfs"));
    Require(astl::SetEnvVar(astl::EnvVar::ASTL_LOG_CONSOLE, "off"));
    minimal_ = BuildProfile("procfs");
    mixed_   = BuildProfile("procfs,scmi");
  }

  ~DiscoveryFixture() { astl::Orchestrator::ResetInstance(); }

  void BeginIteration(bool mixed) {
    procfs_.Reset();
    auto& selected = mixed ? mixed_ : minimal_;
    if (!selected || astl::Orchestrator::SwapInstanceForTest(std::move(selected))) {
      Fail();
    }
    mixed_iteration_ = mixed;
  }

  void EndIteration() {
    auto selected = astl::Orchestrator::SwapInstanceForTest(nullptr);
    if (!selected) {
      Fail();
    }
    (mixed_iteration_ ? mixed_ : minimal_) = std::move(selected);
  }

  void ResetProcfs() { procfs_.Reset(); }

  DiscoveryFixture(const DiscoveryFixture&)            = delete;
  DiscoveryFixture& operator=(const DiscoveryFixture&) = delete;
  DiscoveryFixture(DiscoveryFixture&&)                 = delete;
  DiscoveryFixture& operator=(DiscoveryFixture&&)      = delete;

 private:
  static auto BuildProfile(std::string_view collectors) -> std::unique_ptr<astl::Orchestrator> {
    Require(astl::SetEnvVar(astl::EnvVar::ASTL_COLLECTORS, std::string{collectors}));
    astl::Orchestrator::ResetInstance();
    const auto orchestrator = astl::Orchestrator::GetInstance();
    if (!orchestrator) {
      Fail();
    }
    auto profile = astl::Orchestrator::SwapInstanceForTest(nullptr);
    if (!profile) {
      Fail();
    }
    return profile;
  }

  astl::fuzz::MinimalProcfsFixture    procfs_;
  std::unique_ptr<astl::Orchestrator> minimal_;
  std::unique_ptr<astl::Orchestrator> mixed_;
  bool                                mixed_iteration_{false};
};

auto GetFixture() -> DiscoveryFixture& {
  static DiscoveryFixture fixture;
  return fixture;
}

class IterationFixture {
 public:
  explicit IterationFixture(bool mixed) { GetFixture().BeginIteration(mixed); }

  ~IterationFixture() { GetFixture().EndIteration(); }

  IterationFixture(const IterationFixture&)            = delete;
  IterationFixture& operator=(const IterationFixture&) = delete;
  IterationFixture(IterationFixture&&)                 = delete;
  IterationFixture& operator=(IterationFixture&&)      = delete;
};

auto RunOperation(Operation operation, ByteCursor& input, const TopologySnapshot& topology) -> void {
  switch (operation) {
    case Operation::SYSTEM_INFO:
      FuzzSystemInfo(input, topology);
      break;
    case Operation::TARGETS:
      FuzzTargets(input, topology);
      break;
    case Operation::COUNTERS:
      FuzzCounters(input, topology);
      break;
    case Operation::METRICS:
      FuzzMetrics(input, topology);
      break;
    case Operation::METRIC_STATES:
      FuzzMetricStates(input, topology);
      break;
    case Operation::GLOBAL_METRIC_GROUPS:
      FuzzGroups(input, topology, false);
      break;
    case Operation::TARGET_METRIC_GROUPS:
      FuzzGroups(input, topology, true);
      break;
    case Operation::GLOBAL_GROUP_METRICS:
      FuzzGroupMetrics(input, topology, false);
      break;
    case Operation::TARGET_GROUP_METRICS:
      FuzzGroupMetrics(input, topology, true);
      break;
    case Operation::REPEAT_DISCOVERY:
      CheckTopology(CaptureTopology(), topology);
      break;
    case Operation::COUNT:
      Fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  ByteCursor       input{data, size};
  IterationFixture fixture{input.Take() % 2U != 0U};
  const auto       topology = CaptureTopology();

  const auto operation_count = static_cast<size_t>(input.Take() % kMaxOperations) + 1U;
  for (size_t index = 0; index < operation_count; ++index) {
    const auto operation = static_cast<Operation>(input.Take() % static_cast<uint8_t>(Operation::COUNT));
    RunOperation(operation, input, topology);
  }

  // Fixture reset must not change discovery results or invalidate the live topology.
  GetFixture().ResetProcfs();
  CheckTopology(CaptureTopology(), topology);
  return 0;
}
