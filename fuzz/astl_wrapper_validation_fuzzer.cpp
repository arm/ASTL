// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_test_hooks.h"
#include "astl/astl_version.h"
#include "collector/collector_manager.hpp"
#include "common/capabilities.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"
#include "topology/topology_manager.hpp"

namespace {

constexpr size_t   kMaxOperations             = 48;
constexpr size_t   kCapacity                  = 2;
constexpr uint32_t kBitsPerByte               = 8;
constexpr uint8_t  kValidStructSizeChoices    = 7;
constexpr uint8_t  kUndersizeStructSizeChoice = 7;
constexpr uint8_t  kOversizeStructSizeChoice  = 8;
constexpr uint8_t  kDefaultNullFrequency      = 5;
constexpr uint8_t  kOuterNullFrequency        = 8;
constexpr uint8_t  kStructSizeVariantCount    = 10;

enum class LifecycleOperation : uint8_t {
  READ_IMMEDIATE_ON_TARGET,
  READ_IMMEDIATE,
  START_COLLECTION_ON_TARGET,
  START_COLLECTION,
  START_COLLECTION_ON_TARGET_PAUSED,
  START_COLLECTION_PAUSED,
  PAUSE_COLLECTION_ON_TARGET,
  PAUSE_COLLECTION,
  RESUME_COLLECTION_ON_TARGET,
  RESUME_COLLECTION,
  STOP_COLLECTION_ON_TARGET,
  STOP_COLLECTION,
  COUNT,
};

enum class FuzzOperation : uint8_t {
  STATUS_AND_VERSION,
  SYSTEM_INFO,
  TARGETS,
  COUNTER_DISCOVERY,
  METRIC_DISCOVERY,
  METRIC_STATES,
  METRIC_GROUPS,
  METRIC_GROUP_MEMBERS,
  COUNTER_CONFIGURATION,
  METRIC_CONFIGURATION,
  METRIC_GROUP_CONFIGURATION,
  COLLECTION_LIFECYCLE,
  PERSISTENCE,
  SAMPLES,
  METRIC_SUMMARIES_OR_CROP,
  COUNT,
};

class ByteCursor {
 public:
  ByteCursor(const uint8_t* data, size_t size) : data_{data, size} {}

  auto Take() -> uint8_t {
    if (offset_ >= data_.size()) {
      return 0;
    }
    return data_[offset_++];
  }

  auto TakeUint32() -> uint32_t {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
      value |= static_cast<uint32_t>(Take()) << (index * kBitsPerByte);
    }
    return value;
  }

  auto TakeUint64() -> uint64_t {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
      value |= static_cast<uint64_t>(Take()) << (index * kBitsPerByte);
    }
    return value;
  }

 private:
  std::span<const uint8_t> data_;
  size_t                   offset_{0};
};

template <typename StructT>
auto ChooseStructSize(ByteCursor& input) -> size_t {
  const auto choice = input.Take() % kStructSizeVariantCount;
  if (choice < kValidStructSizeChoices) {
    return sizeof(StructT);
  }
  if (choice == kUndersizeStructSizeChoice) {
    return sizeof(StructT) - 1U;
  }
  if (choice == kOversizeStructSizeChoice) {
    return sizeof(StructT) + 1U;
  }
  return 0;
}

auto ChooseFlags(ByteCursor& input) -> uint32_t { return input.Take() % 4U == 0U ? (input.TakeUint32() | 1U) : 0U; }

template <typename T>
auto MaybeNull(ByteCursor& input, T* pointer, uint8_t frequency = kDefaultNullFrequency) -> T* {
  return input.Take() % frequency == 0U ? nullptr : pointer;
}

auto ChooseTarget(ByteCursor& input, astl_target_handle_t stable_target) -> astl_target_handle_t {
  return input.Take() % 4U == 0U ? nullptr : stable_target;
}

auto ChooseOpaqueHandle(ByteCursor& input, astl_target_handle_t stable_target) -> const void* {
  // A target handle is a stable, controlled cross-type value. Public APIs must reject it without dereferencing
  // fuzzer-generated addresses; null remains reachable as the ordinary missing-handle case.
  return input.Take() % 4U == 0U ? nullptr : stable_target;
}

auto ChooseTimestamps(ByteCursor& input) -> std::pair<uint64_t, uint64_t> {
  const auto first  = input.TakeUint64();
  const auto second = input.TakeUint64();
  switch (input.Take() % 4U) {
    case 0:
      return {0, 0};
    case 1:
      return {std::min(first, second), std::max(first, second)};
    case 2:
      return {std::max(first, second), std::min(first, second)};
    default:
      return {first, 0};
  }
}

auto CheckPublicStatus(astl_status_code status) -> void {
  const auto value = static_cast<int>(status);
  if (value < static_cast<int>(ASTL_STATUS_SUCCESS) || value > static_cast<int>(ASTL_STATUS_INTERNAL_ERROR)) {
    __builtin_trap();
  }
}

template <typename ParamsT, typename FunctionT>
auto CallSimpleApi(ByteCursor& input, FunctionT function) -> void {
  ParamsT params{.size = ChooseStructSize<ParamsT>(input), .flags = ChooseFlags(input)};
  CheckPublicStatus(function(MaybeNull(input, &params, kOuterNullFrequency)));
}

template <typename ParamsT, typename FunctionT>
auto CallTargetApi(ByteCursor& input, astl_target_handle_t stable_target, FunctionT function) -> void {
  ParamsT params{.size          = ChooseStructSize<ParamsT>(input),
                 .flags         = ChooseFlags(input),
                 .target_handle = ChooseTarget(input, stable_target)};
  CheckPublicStatus(function(MaybeNull(input, &params, kOuterNullFrequency)));
}

auto MakeOrchestrator(bool include_target) -> std::unique_ptr<astl::Orchestrator> {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  if (include_target) {
    targets.push_back(std::make_unique<astl::Target>("fuzz-target", "deterministic wrapper-validation target",
                                                     astl::CollectorType::ASTL_NATIVE, nullptr, "fuzz-target-0"));
  }

  auto topology_manager  = std::make_unique<astl::TopologyManager>(std::move(targets));
  auto collector_manager = std::make_unique<astl::CollectorManager>(
      std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>>{});
  astl::Capabilities capabilities{std::vector<astl::CollectorCapability>{}, std::vector<astl::SystemCapability>{}};
  auto               metric_manager = std::make_unique<astl::MetricManager>(capabilities);
  auto               output_manager = std::make_unique<astl::OutputManager>();

  return std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                              std::move(metric_manager), std::move(output_manager), "");
}

class ScopedOrchestratorInjection {
 public:
  ScopedOrchestratorInjection() {
    previous_process_orchestrator_ = astl::Orchestrator::SwapInstanceForTest(MakeOrchestrator(false));

    auto  injected_orchestrator = MakeOrchestrator(true);
    auto* injected_raw          = injected_orchestrator.get();
    if (astlInjectTestOrchestrator(injected_raw, &baseline_orchestrator_) != ASTL_STATUS_SUCCESS) {
      __builtin_trap();
    }
    (void)injected_orchestrator.release();
  }

  ~ScopedOrchestratorInjection() {
    astl_test_orchestrator_t injected_handle = nullptr;
    if (astlInjectTestOrchestrator(baseline_orchestrator_, &injected_handle) != ASTL_STATUS_SUCCESS) {
      __builtin_trap();
    }
    std::unique_ptr<astl::Orchestrator> injected_orchestrator{static_cast<astl::Orchestrator*>(injected_handle)};
    (void)astl::Orchestrator::SwapInstanceForTest(std::move(previous_process_orchestrator_));
  }

  ScopedOrchestratorInjection(const ScopedOrchestratorInjection&)            = delete;
  ScopedOrchestratorInjection& operator=(const ScopedOrchestratorInjection&) = delete;
  ScopedOrchestratorInjection(ScopedOrchestratorInjection&&)                 = delete;
  ScopedOrchestratorInjection& operator=(ScopedOrchestratorInjection&&)      = delete;

 private:
  std::unique_ptr<astl::Orchestrator> previous_process_orchestrator_;
  astl_test_orchestrator_t            baseline_orchestrator_{nullptr};
};

auto GetStableTargetHandle() -> astl_target_handle_t {
  uint32_t                       target_count = 0;
  astl_get_target_count_params_t count_params{
      .size = sizeof(astl_get_target_count_params_t), .flags = 0, .target_count = &target_count};
  if (astlGetTargetCount(&count_params) != ASTL_STATUS_SUCCESS || target_count != 1U) {
    __builtin_trap();
  }

  std::array<astl_target_props_t, 1> targets{};
  targets.front().size = sizeof(astl_target_props_t);
  astl_get_targets_params_t targets_params{
      .size = sizeof(astl_get_targets_params_t), .flags = 0, .targets = targets.data(), .target_count = &target_count};
  if (astlGetTargets(&targets_params) != ASTL_STATUS_SUCCESS || target_count != 1U ||
      targets.front().handle == nullptr) {
    __builtin_trap();
  }
  return targets.front().handle;
}

auto FuzzStatusAndVersion(ByteCursor& input) -> void {
  const auto status = static_cast<astl_status_code>(static_cast<int8_t>(input.Take()));
  if (astlStatusString(status) == nullptr || astlGetLastStatusString() == nullptr || astlVersionString() == nullptr) {
    __builtin_trap();
  }
  (void)astlVersion();
}

auto FuzzSystemInfo(ByteCursor& input) -> void {
  astl_platform_props_t platform{};
  platform.size  = ChooseStructSize<astl_platform_props_t>(input);
  platform.flags = input.Take() % 3U == 0U ? UINT32_MAX : ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
  astl_get_system_info_params_t params{
      .size        = ChooseStructSize<astl_get_system_info_params_t>(input),
      .flags       = ChooseFlags(input),
      .system_info = MaybeNull(input, &platform),
  };
  CheckPublicStatus(astlGetSystemInfo(MaybeNull(input, &params, kOuterNullFrequency)));
}

auto FuzzTargets(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  uint32_t                       target_count = input.TakeUint32();
  astl_get_target_count_params_t count_params{
      .size         = ChooseStructSize<astl_get_target_count_params_t>(input),
      .flags        = ChooseFlags(input),
      .target_count = MaybeNull(input, &target_count),
  };
  auto status = astlGetTargetCount(MaybeNull(input, &count_params, kOuterNullFrequency));
  CheckPublicStatus(status);
  if (status == ASTL_STATUS_SUCCESS && target_count != 1U) {
    __builtin_trap();
  }

  std::array<astl_target_props_t, kCapacity> targets{};
  targets.front().size               = ChooseStructSize<astl_target_props_t>(input);
  target_count                       = input.Take() % (kCapacity + 1U);
  const auto                capacity = target_count;
  astl_get_targets_params_t params{
      .size         = ChooseStructSize<astl_get_targets_params_t>(input),
      .flags        = ChooseFlags(input),
      .targets      = MaybeNull(input, targets.data()),
      .target_count = MaybeNull(input, &target_count),
  };
  status = astlGetTargets(MaybeNull(input, &params, kOuterNullFrequency));
  CheckPublicStatus(status);
  if (status == ASTL_STATUS_SUCCESS &&
      (target_count > capacity || target_count != 1U || targets.front().handle != stable_target)) {
    __builtin_trap();
  }
}

auto FuzzCounterDiscovery(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  uint32_t                        counter_count = input.TakeUint32();
  astl_get_counter_count_params_t count_params{
      .size          = ChooseStructSize<astl_get_counter_count_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .counter_count = MaybeNull(input, &counter_count),
  };
  auto status = astlGetCounterCountOnTarget(MaybeNull(input, &count_params, kOuterNullFrequency));
  CheckPublicStatus(status);
  if (status == ASTL_STATUS_SUCCESS && counter_count != 0U) {
    __builtin_trap();
  }

  std::array<astl_counter_props_t, kCapacity> counters{};
  counters.front().size = ChooseStructSize<astl_counter_props_t>(input);
  counter_count         = input.Take() % (kCapacity + 1U);
  astl_get_counters_params_t params{
      .size          = ChooseStructSize<astl_get_counters_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .counters      = MaybeNull(input, counters.data()),
      .counter_count = MaybeNull(input, &counter_count),
  };
  CheckPublicStatus(astlGetCountersOnTarget(MaybeNull(input, &params, kOuterNullFrequency)));
}

auto FuzzMetricDiscovery(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  uint32_t                       metric_count = input.TakeUint32();
  astl_get_metric_count_params_t count_params{
      .size          = ChooseStructSize<astl_get_metric_count_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_count  = MaybeNull(input, &metric_count),
  };
  auto status = astlGetMetricCountOnTarget(MaybeNull(input, &count_params, kOuterNullFrequency));
  CheckPublicStatus(status);
  if (status == ASTL_STATUS_SUCCESS && metric_count > 1U) {
    __builtin_trap();
  }

  std::array<astl_metric_props_t, kCapacity> metrics{};
  metrics.front().size = ChooseStructSize<astl_metric_props_t>(input);
  metric_count         = input.Take() % (kCapacity + 1U);
  astl_get_metrics_params_t params{
      .size          = ChooseStructSize<astl_get_metrics_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metrics       = MaybeNull(input, metrics.data()),
      .metric_count  = MaybeNull(input, &metric_count),
  };
  CheckPublicStatus(astlGetMetricsOnTarget(MaybeNull(input, &params, kOuterNullFrequency)));
}

auto FuzzMetricStates(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  const astl_metric_handle_t metric_handle =
      static_cast<astl_metric_handle_t>(ChooseOpaqueHandle(input, stable_target));
  uint32_t                                       state_count = input.TakeUint32();
  astl_get_metric_state_count_on_target_params_t count_params{
      .size          = ChooseStructSize<astl_get_metric_state_count_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = metric_handle,
      .state_count   = MaybeNull(input, &state_count),
  };
  CheckPublicStatus(astlGetMetricStateCountOnTarget(MaybeNull(input, &count_params, kOuterNullFrequency)));

  std::array<astl_state_props_t, kCapacity> states{};
  states.front().size = ChooseStructSize<astl_state_props_t>(input);
  state_count         = input.Take() % (kCapacity + 1U);
  astl_get_metric_states_on_target_params_t params{
      .size          = ChooseStructSize<astl_get_metric_states_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = metric_handle,
      .states        = MaybeNull(input, states.data()),
      .state_count   = MaybeNull(input, &state_count),
  };
  CheckPublicStatus(astlGetMetricStatesOnTarget(MaybeNull(input, &params, kOuterNullFrequency)));
}

auto FuzzMetricGroups(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  uint32_t                             group_count = input.TakeUint32();
  astl_get_metric_group_count_params_t global_count{
      .size               = ChooseStructSize<astl_get_metric_group_count_params_t>(input),
      .flags              = ChooseFlags(input),
      .metric_group_count = MaybeNull(input, &group_count),
  };
  CheckPublicStatus(astlGetMetricGroupCount(MaybeNull(input, &global_count, kOuterNullFrequency)));

  astl_get_metric_group_count_on_target_params_t target_count{
      .size               = ChooseStructSize<astl_get_metric_group_count_on_target_params_t>(input),
      .flags              = ChooseFlags(input),
      .target_handle      = ChooseTarget(input, stable_target),
      .metric_group_count = MaybeNull(input, &group_count),
  };
  CheckPublicStatus(astlGetMetricGroupCountOnTarget(MaybeNull(input, &target_count, kOuterNullFrequency)));

  std::array<astl_metric_group_props_t, kCapacity> groups{};
  groups.front().size = ChooseStructSize<astl_metric_group_props_t>(input);
  group_count         = input.Take() % (kCapacity + 1U);
  astl_get_metric_groups_params_t global_groups{
      .size               = ChooseStructSize<astl_get_metric_groups_params_t>(input),
      .flags              = ChooseFlags(input),
      .metric_groups      = MaybeNull(input, groups.data()),
      .metric_group_count = MaybeNull(input, &group_count),
  };
  CheckPublicStatus(astlGetMetricGroups(MaybeNull(input, &global_groups, kOuterNullFrequency)));

  group_count = input.Take() % (kCapacity + 1U);
  astl_get_metric_groups_on_target_params_t target_groups{
      .size               = ChooseStructSize<astl_get_metric_groups_on_target_params_t>(input),
      .flags              = ChooseFlags(input),
      .target_handle      = ChooseTarget(input, stable_target),
      .metric_groups      = MaybeNull(input, groups.data()),
      .metric_group_count = MaybeNull(input, &group_count),
  };
  CheckPublicStatus(astlGetMetricGroupsOnTarget(MaybeNull(input, &target_groups, kOuterNullFrequency)));
}

auto FuzzMetricGroupMembers(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  const astl_metric_group_handle_t group_handle =
      static_cast<astl_metric_group_handle_t>(ChooseOpaqueHandle(input, stable_target));
  uint32_t                                    metric_count = input.TakeUint32();
  astl_get_metric_group_metric_count_params_t global_count{
      .size                = ChooseStructSize<astl_get_metric_group_metric_count_params_t>(input),
      .flags               = ChooseFlags(input),
      .metric_group_handle = group_handle,
      .metric_count        = MaybeNull(input, &metric_count),
  };
  CheckPublicStatus(astlGetMetricGroupMetricCount(MaybeNull(input, &global_count, kOuterNullFrequency)));

  astl_get_metric_group_metric_count_on_target_params_t target_count{
      .size                = ChooseStructSize<astl_get_metric_group_metric_count_on_target_params_t>(input),
      .flags               = ChooseFlags(input),
      .target_handle       = ChooseTarget(input, stable_target),
      .metric_group_handle = group_handle,
      .metric_count        = MaybeNull(input, &metric_count),
  };
  CheckPublicStatus(astlGetMetricGroupMetricCountOnTarget(MaybeNull(input, &target_count, kOuterNullFrequency)));

  std::array<astl_metric_props_t, kCapacity> metrics{};
  metrics.front().size = ChooseStructSize<astl_metric_props_t>(input);
  metric_count         = input.Take() % (kCapacity + 1U);
  astl_get_metric_group_metrics_params_t global_metrics{
      .size                = ChooseStructSize<astl_get_metric_group_metrics_params_t>(input),
      .flags               = ChooseFlags(input),
      .metric_group_handle = group_handle,
      .metrics             = MaybeNull(input, metrics.data()),
      .metric_count        = MaybeNull(input, &metric_count),
  };
  CheckPublicStatus(astlGetMetricGroupMetrics(MaybeNull(input, &global_metrics, kOuterNullFrequency)));

  metric_count = input.Take() % (kCapacity + 1U);
  astl_get_metric_group_metrics_on_target_params_t target_metrics{
      .size                = ChooseStructSize<astl_get_metric_group_metrics_on_target_params_t>(input),
      .flags               = ChooseFlags(input),
      .target_handle       = ChooseTarget(input, stable_target),
      .metric_group_handle = group_handle,
      .metrics             = MaybeNull(input, metrics.data()),
      .metric_count        = MaybeNull(input, &metric_count),
  };
  CheckPublicStatus(astlGetMetricGroupMetricsOnTarget(MaybeNull(input, &target_metrics, kOuterNullFrequency)));
}

auto MakeCollectionParams(ByteCursor& input) -> astl_collection_params_t {
  return astl_collection_params_t{
      .size              = ChooseStructSize<astl_collection_params_t>(input),
      .flags             = input.Take() % 4U == 0U ? input.TakeUint32() : ASTL_COLLECTION_PARAMETERS_FLAG_NONE,
      .sampling_interval = input.TakeUint32(),
      .collection_mode   = static_cast<astl_collection_mode_t>(static_cast<int8_t>(input.Take())),
  };
}

auto FuzzCounterConfiguration(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  auto                                               collection = MakeCollectionParams(input);
  const std::array<astl_counter_handle_t, kCapacity> handles{
      static_cast<astl_counter_handle_t>(ChooseOpaqueHandle(input, stable_target)), nullptr};
  astl_configure_counter_collection_on_target_params_t target_params{
      .size              = ChooseStructSize<astl_configure_counter_collection_on_target_params_t>(input),
      .flags             = ChooseFlags(input),
      .target_handle     = ChooseTarget(input, stable_target),
      .collection_params = MaybeNull(input, &collection),
      .counter_handles   = MaybeNull(input, handles.data()),
      .counter_count     = input.Take() % 3U,
  };
  CheckPublicStatus(astlConfigureCounterCollectionOnTarget(MaybeNull(input, &target_params, kOuterNullFrequency)));

  astl_configure_counter_collection_params_t global_params{
      .size              = ChooseStructSize<astl_configure_counter_collection_params_t>(input),
      .flags             = ChooseFlags(input),
      .collection_params = MaybeNull(input, &collection),
      .counter_handles   = MaybeNull(input, handles.data()),
      .counter_count     = input.Take() % 3U,
  };
  CheckPublicStatus(astlConfigureCounterCollection(MaybeNull(input, &global_params, kOuterNullFrequency)));
}

auto FuzzMetricConfiguration(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  auto                                              collection = MakeCollectionParams(input);
  const std::array<astl_metric_handle_t, kCapacity> handles{
      static_cast<astl_metric_handle_t>(ChooseOpaqueHandle(input, stable_target)), nullptr};
  astl_configure_metric_collection_on_target_params_t target_params{
      .size              = ChooseStructSize<astl_configure_metric_collection_on_target_params_t>(input),
      .flags             = ChooseFlags(input),
      .target_handle     = ChooseTarget(input, stable_target),
      .collection_params = MaybeNull(input, &collection),
      .metric_handles    = MaybeNull(input, handles.data()),
      .metric_count      = input.Take() % 3U,
  };
  CheckPublicStatus(astlConfigureMetricCollectionOnTarget(MaybeNull(input, &target_params, kOuterNullFrequency)));

  astl_configure_metric_collection_params_t global_params{
      .size              = ChooseStructSize<astl_configure_metric_collection_params_t>(input),
      .flags             = ChooseFlags(input),
      .collection_params = MaybeNull(input, &collection),
      .metric_handles    = MaybeNull(input, handles.data()),
      .metric_count      = input.Take() % 3U,
  };
  CheckPublicStatus(astlConfigureMetricCollection(MaybeNull(input, &global_params, kOuterNullFrequency)));
}

auto FuzzMetricGroupConfiguration(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  auto                                                    collection = MakeCollectionParams(input);
  const std::array<astl_metric_group_handle_t, kCapacity> handles{
      static_cast<astl_metric_group_handle_t>(ChooseOpaqueHandle(input, stable_target)), nullptr};
  astl_configure_metric_group_collection_on_target_params_t target_params{
      .size                 = ChooseStructSize<astl_configure_metric_group_collection_on_target_params_t>(input),
      .flags                = ChooseFlags(input),
      .target_handle        = ChooseTarget(input, stable_target),
      .collection_params    = MaybeNull(input, &collection),
      .metric_group_handles = MaybeNull(input, handles.data()),
      .metric_group_count   = input.Take() % 3U,
  };
  CheckPublicStatus(astlConfigureMetricGroupCollectionOnTarget(MaybeNull(input, &target_params, kOuterNullFrequency)));

  astl_configure_metric_group_collection_params_t global_params{
      .size                 = ChooseStructSize<astl_configure_metric_group_collection_params_t>(input),
      .flags                = ChooseFlags(input),
      .collection_params    = MaybeNull(input, &collection),
      .metric_group_handles = MaybeNull(input, handles.data()),
      .metric_group_count   = input.Take() % 3U,
  };
  CheckPublicStatus(astlConfigureMetricGroupCollection(MaybeNull(input, &global_params, kOuterNullFrequency)));
}

auto FuzzCollectionLifecycle(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  const auto operation =
      static_cast<LifecycleOperation>(input.Take() % static_cast<uint8_t>(LifecycleOperation::COUNT));
  switch (operation) {
    case LifecycleOperation::READ_IMMEDIATE_ON_TARGET:
      CallTargetApi<astl_read_immediate_on_target_params_t>(input, stable_target, astlReadImmediateOnTarget);
      break;
    case LifecycleOperation::READ_IMMEDIATE:
      CallSimpleApi<astl_read_immediate_params_t>(input, astlReadImmediate);
      break;
    case LifecycleOperation::START_COLLECTION_ON_TARGET:
      CallTargetApi<astl_start_collection_on_target_params_t>(input, stable_target, astlStartCollectionOnTarget);
      break;
    case LifecycleOperation::START_COLLECTION:
      CallSimpleApi<astl_start_collection_params_t>(input, astlStartCollection);
      break;
    case LifecycleOperation::START_COLLECTION_ON_TARGET_PAUSED:
      CallTargetApi<astl_start_collection_on_target_paused_params_t>(input, stable_target,
                                                                     astlStartCollectionOnTargetPaused);
      break;
    case LifecycleOperation::START_COLLECTION_PAUSED:
      CallSimpleApi<astl_start_collection_paused_params_t>(input, astlStartCollectionPaused);
      break;
    case LifecycleOperation::PAUSE_COLLECTION_ON_TARGET:
      CallTargetApi<astl_pause_collection_on_target_params_t>(input, stable_target, astlPauseCollectionOnTarget);
      break;
    case LifecycleOperation::PAUSE_COLLECTION:
      CallSimpleApi<astl_pause_collection_params_t>(input, astlPauseCollection);
      break;
    case LifecycleOperation::RESUME_COLLECTION_ON_TARGET:
      CallTargetApi<astl_resume_collection_on_target_params_t>(input, stable_target, astlResumeCollectionOnTarget);
      break;
    case LifecycleOperation::RESUME_COLLECTION:
      CallSimpleApi<astl_resume_collection_params_t>(input, astlResumeCollection);
      break;
    case LifecycleOperation::STOP_COLLECTION_ON_TARGET:
      CallTargetApi<astl_stop_collection_on_target_params_t>(input, stable_target, astlStopCollectionOnTarget);
      break;
    case LifecycleOperation::STOP_COLLECTION:
      CallSimpleApi<astl_stop_collection_params_t>(input, astlStopCollection);
      break;
    case LifecycleOperation::COUNT:
      __builtin_unreachable();
  }
}

auto FuzzPersistence(ByteCursor& input) -> void {
  const char*        path = input.Take() % 2U == 0U ? nullptr : "";
  astl_save_params_t save_params{
      .size = ChooseStructSize<astl_save_params_t>(input), .flags = ChooseFlags(input), .output_file_path = path};
  CheckPublicStatus(astlSaveCollection(MaybeNull(input, &save_params, kOuterNullFrequency)));

  astl_load_params_t load_params{
      .size             = ChooseStructSize<astl_load_params_t>(input),
      .flags            = ChooseFlags(input),
      .input_file_path  = path,
      .chunk_size_bytes = input.TakeUint32(),
  };
  CheckPublicStatus(astlLoadCollection(MaybeNull(input, &load_params, kOuterNullFrequency)));
}

auto FuzzSamples(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  const void* const                    opaque_handle = ChooseOpaqueHandle(input, stable_target);
  uint32_t                             sample_count  = input.Take() % (kCapacity + 1U);
  const auto                           timestamps    = ChooseTimestamps(input);
  std::array<astl_sample_t, kCapacity> samples{};

  astl_get_counter_sample_count_on_target_params_t counter_count{
      .size           = ChooseStructSize<astl_get_counter_sample_count_on_target_params_t>(input),
      .flags          = ChooseFlags(input),
      .target_handle  = ChooseTarget(input, stable_target),
      .counter_handle = static_cast<astl_counter_handle_t>(opaque_handle),
      .sample_count   = MaybeNull(input, &sample_count),
      .start_ts       = timestamps.first,
      .end_ts         = timestamps.second,
  };
  CheckPublicStatus(astlGetCounterSampleCountOnTarget(MaybeNull(input, &counter_count, kOuterNullFrequency)));

  astl_get_counter_samples_on_target_params_t counters{
      .size           = ChooseStructSize<astl_get_counter_samples_on_target_params_t>(input),
      .flags          = ChooseFlags(input),
      .target_handle  = ChooseTarget(input, stable_target),
      .counter_handle = static_cast<astl_counter_handle_t>(opaque_handle),
      .samples        = MaybeNull(input, samples.data()),
      .sample_count   = MaybeNull(input, &sample_count),
      .start_ts       = timestamps.first,
      .end_ts         = timestamps.second,
  };
  CheckPublicStatus(astlGetCounterSamplesOnTarget(MaybeNull(input, &counters, kOuterNullFrequency)));

  astl_get_metric_sample_count_on_target_params_t metric_count{
      .size          = ChooseStructSize<astl_get_metric_sample_count_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = static_cast<astl_metric_handle_t>(opaque_handle),
      .sample_count  = MaybeNull(input, &sample_count),
      .start_ts      = timestamps.first,
      .end_ts        = timestamps.second,
  };
  CheckPublicStatus(astlGetMetricSampleCountOnTarget(MaybeNull(input, &metric_count, kOuterNullFrequency)));

  astl_get_metric_samples_on_target_params_t metrics{
      .size          = ChooseStructSize<astl_get_metric_samples_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = static_cast<astl_metric_handle_t>(opaque_handle),
      .samples       = MaybeNull(input, samples.data()),
      .sample_count  = MaybeNull(input, &sample_count),
      .start_ts      = timestamps.first,
      .end_ts        = timestamps.second,
  };
  CheckPublicStatus(astlGetMetricSamplesOnTarget(MaybeNull(input, &metrics, kOuterNullFrequency)));
}

auto FuzzMetricSummaries(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  const astl_metric_handle_t metric_handle =
      static_cast<astl_metric_handle_t>(ChooseOpaqueHandle(input, stable_target));
  const auto               timestamps = ChooseTimestamps(input);
  astl_metric_statistics_t summary{};
  summary.size  = ChooseStructSize<astl_metric_statistics_t>(input);
  summary.flags = input.Take() % 3U == 0U ? input.TakeUint32() : ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG;
  astl_get_metric_statistics_on_target_params_t params{
      .size          = ChooseStructSize<astl_get_metric_statistics_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = metric_handle,
      .summary       = MaybeNull(input, &summary),
      .start_ts      = timestamps.first,
      .end_ts        = timestamps.second,
  };
  CheckPublicStatus(astlGetMetricStatisticsOnTarget(MaybeNull(input, &params, kOuterNullFrequency)));

  uint32_t                                                        bin_count = input.Take() % (kCapacity + 1U);
  astl_get_metric_discrete_histogram_bin_count_on_target_params_t count_params{
      .size          = ChooseStructSize<astl_get_metric_discrete_histogram_bin_count_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = metric_handle,
      .bin_count     = MaybeNull(input, &bin_count),
      .start_ts      = timestamps.first,
      .end_ts        = timestamps.second,
  };
  CheckPublicStatus(
      astlGetMetricDiscreteHistogramBinCountOnTarget(MaybeNull(input, &count_params, kOuterNullFrequency)));

  std::array<astl_discrete_histogram_bin_t, kCapacity> bins{};
  bins.front().size = ChooseStructSize<astl_discrete_histogram_bin_t>(input);
  astl_get_metric_discrete_histogram_on_target_params_t histogram_params{
      .size          = ChooseStructSize<astl_get_metric_discrete_histogram_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = metric_handle,
      .bins          = MaybeNull(input, bins.data()),
      .bin_count     = MaybeNull(input, &bin_count),
      .start_ts      = timestamps.first,
      .end_ts        = timestamps.second,
  };
  CheckPublicStatus(astlGetMetricDiscreteHistogramOnTarget(MaybeNull(input, &histogram_params, kOuterNullFrequency)));
}

auto FuzzCrop(ByteCursor& input, astl_target_handle_t stable_target) -> void {
  std::array<astl_crop_window_t, kCapacity> windows{};
  const auto                                timestamps = ChooseTimestamps(input);
  windows.front().size                                 = ChooseStructSize<astl_crop_window_t>(input);
  windows.front().flags                                = ChooseFlags(input);
  windows.front().start_ts                             = timestamps.first;
  windows.front().end_ts                               = timestamps.second;
  windows.back()                                       = windows.front();
  const auto window_count                              = static_cast<uint32_t>(input.Take() % (kCapacity + 1U));

  astl_crop_samples_on_target_params_t target_params{
      .size          = ChooseStructSize<astl_crop_samples_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .windows       = MaybeNull(input, windows.data()),
      .window_count  = window_count,
  };
  CheckPublicStatus(astlCropSamplesOnTarget(MaybeNull(input, &target_params, kOuterNullFrequency)));

  astl_crop_metric_samples_on_target_params_t metric_params{
      .size          = ChooseStructSize<astl_crop_metric_samples_on_target_params_t>(input),
      .flags         = ChooseFlags(input),
      .target_handle = ChooseTarget(input, stable_target),
      .metric_handle = static_cast<astl_metric_handle_t>(ChooseOpaqueHandle(input, stable_target)),
      .windows       = MaybeNull(input, windows.data()),
      .window_count  = window_count,
  };
  CheckPublicStatus(astlCropMetricSamplesOnTarget(MaybeNull(input, &metric_params, kOuterNullFrequency)));

  astl_crop_samples_params_t global_params{
      .size         = ChooseStructSize<astl_crop_samples_params_t>(input),
      .flags        = ChooseFlags(input),
      .windows      = MaybeNull(input, windows.data()),
      .window_count = window_count,
  };
  CheckPublicStatus(astlCropSamples(MaybeNull(input, &global_params, kOuterNullFrequency)));
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) -> int {
  ByteCursor input{data, size};
  // The fixture is process-lifetime and all mutating operations are constrained to fail validation. Reconstructing
  // it for every input would repeatedly intern identical target metadata and obscure the API coverage signal.
  static const auto* injection = new ScopedOrchestratorInjection;
  (void)injection;
  const astl_target_handle_t stable_target   = GetStableTargetHandle();
  const size_t               operation_count = 1U + (input.Take() % kMaxOperations);

  for (size_t index = 0; index < operation_count; ++index) {
    const auto operation = static_cast<FuzzOperation>(input.Take() % static_cast<uint8_t>(FuzzOperation::COUNT));
    switch (operation) {
      case FuzzOperation::STATUS_AND_VERSION:
        FuzzStatusAndVersion(input);
        break;
      case FuzzOperation::SYSTEM_INFO:
        FuzzSystemInfo(input);
        break;
      case FuzzOperation::TARGETS:
        FuzzTargets(input, stable_target);
        break;
      case FuzzOperation::COUNTER_DISCOVERY:
        FuzzCounterDiscovery(input, stable_target);
        break;
      case FuzzOperation::METRIC_DISCOVERY:
        FuzzMetricDiscovery(input, stable_target);
        break;
      case FuzzOperation::METRIC_STATES:
        FuzzMetricStates(input, stable_target);
        break;
      case FuzzOperation::METRIC_GROUPS:
        FuzzMetricGroups(input, stable_target);
        break;
      case FuzzOperation::METRIC_GROUP_MEMBERS:
        FuzzMetricGroupMembers(input, stable_target);
        break;
      case FuzzOperation::COUNTER_CONFIGURATION:
        FuzzCounterConfiguration(input, stable_target);
        break;
      case FuzzOperation::METRIC_CONFIGURATION:
        FuzzMetricConfiguration(input, stable_target);
        break;
      case FuzzOperation::METRIC_GROUP_CONFIGURATION:
        FuzzMetricGroupConfiguration(input, stable_target);
        break;
      case FuzzOperation::COLLECTION_LIFECYCLE:
        FuzzCollectionLifecycle(input, stable_target);
        break;
      case FuzzOperation::PERSISTENCE:
        FuzzPersistence(input);
        break;
      case FuzzOperation::SAMPLES:
        FuzzSamples(input, stable_target);
        break;
      case FuzzOperation::METRIC_SUMMARIES_OR_CROP:
        input.Take() % 2U == 0U ? FuzzMetricSummaries(input, stable_target) : FuzzCrop(input, stable_target);
        break;
      case FuzzOperation::COUNT:
        __builtin_unreachable();
    }
  }

  return 0;
}
