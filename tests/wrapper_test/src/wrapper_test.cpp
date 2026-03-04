// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_test_hooks.h"
#include "collector/collector_manager.hpp"
#include "common/metric_config.hpp"
#include "common/system_info.hpp"
#include "metric/counter.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "serdes/archive_utils.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "target.hpp"
#include "topology/topology_manager.hpp"

using trompeloeil::_;

template <typename T>
auto AllocateAstlVector(size_t count) -> std::vector<T> {
  std::vector<T> objects{count};
  if (count > 0) {
    objects[0]._size = sizeof(T);
  }
  return objects;
}

using expectation = std::unique_ptr<trompeloeil::expectation>;

auto MakeMinimalOrchestrator(std::unique_ptr<MockMetricManager> metric_manager = nullptr)
    -> std::pair<std::unique_ptr<astl::Orchestrator>, std::vector<expectation>> {
  auto                     topology_manager  = std::make_unique<MockTopologyManager>();
  auto                     collector_manager = std::make_unique<MockCollectorManager>();
  std::vector<expectation> expectations;
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  if (metric_manager == nullptr) {
    metric_manager = std::make_unique<MockMetricManager>();
  }
  expectations.push_back(
      NAMED_ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*metric_manager, RemoveAllMetrics()));
  auto output_manager = std::make_unique<MockOutputManager>();

  return {std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                               std::move(metric_manager), std::move(output_manager), ""),
          std::move(expectations)};
}

// imprecise constants for testing
constexpr uint32_t kJunk = 13;
constexpr uint32_t kAFew = 7;

TEST_CASE("astlVersion", "[matches header definition][wrapper]") {
  astl_version_t version = astlVersion();
  REQUIRE(version._major == ASTL_VERSION_MAJOR);
  REQUIRE(version._minor == ASTL_VERSION_MINOR);
  REQUIRE(version._micro == ASTL_VERSION_MICRO);
}

TEST_CASE("astlVersionString", "[matches header definition][wrapper]") {
  const char* version_string = astlVersionString();
  REQUIRE(std::string(version_string) == std::string(ASTL_VERSION_STRING));
}

TEST_CASE("astlStatusString", "[matches header definition][wrapper]") {
  astl_status_code error        = ASTL_STATUS_BAD_ARGUMENT;
  const char*      error_string = astlStatusString(error);
  REQUIRE(std::string(error_string) == "BAD_ARGUMENT");

  REQUIRE(std::string(astlStatusString(ASTL_STATUS_NO_DATA_COLLECTED)) == "NO_DATA_COLLECTED");
  REQUIRE(std::string(astlStatusString(ASTL_STATUS_UNKNOWN_ERROR)) == "UNKNOWN_ERROR");
  REQUIRE(std::string(astlStatusString(ASTL_STATUS_INTERNAL_ERROR)) == "INTERNAL_ERROR");
  // for now at least, anything about ASTL_STATUS_INTERNAL_ERROR is unknown
  astl_status_code truly_unknown = static_cast<astl_status_code>(ASTL_STATUS_INTERNAL_ERROR + ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(std::string(astlStatusString(truly_unknown)) == "UNKNOWN_ERROR");
}

TEST_CASE("astlGetSystemInfo", "[wrapper][SystemInfo]") {
  REQUIRE(astlGetSystemInfo(nullptr) == ASTL_STATUS_BAD_ARGUMENT);

  astl_platform_properties_t incompatible_size_info{};
  incompatible_size_info._size = sizeof(astl_platform_properties_t) - 1;
  REQUIRE(astlGetSystemInfo(&incompatible_size_info) == ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);

  astl_platform_properties_t system_info{};
  system_info._size = sizeof(astl_platform_properties_t);
  REQUIRE(astlGetSystemInfo(&system_info) == ASTL_STATUS_SUCCESS);

  REQUIRE((system_info._os_name != nullptr || system_info._kernel_name != nullptr ||
           system_info._kernel_release != nullptr || system_info._architecture != nullptr ||
           system_info._hostname != nullptr || system_info._soc_name != nullptr || system_info._vendor_id != nullptr ||
           system_info._firmware_version != nullptr));
}

TEST_CASE("astlGetTargetCount", "[Reports 0 targets correctly][wrapper]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(astlGetTargetCount(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  uint32_t target_count{kJunk};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 0);
}

TEST_CASE("astlGetTargetCount", "[Reports a few targets correctly][wrapper]") {
  // now give it a few targets targets to talk to
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  for (uint32_t i = 0; i < kAFew; ++i) {
    targets.push_back(std::make_unique<MockTarget>());
  }
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t target_count{kJunk};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == kAFew);
}

TEST_CASE("astlGetTargets", "[0 targets available][wrapper]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t                              target_count{kJunk};
  std::vector<astl_target_properties_t> targets{kAFew};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 0);
  // with 0 targets available, asking for some of them causes a special error code
  target_count = kAFew;
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_NO_TARGETS_FOUND);
  REQUIRE(target_count == 0);
}

TEST_CASE("astlGetTargets", "[Oversized buffer][wrapper]") {
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target_1));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  auto actual_target_count = target_count;
  target_count *= 2;  // allocate a little extra buffer, to ensure we get the right warning
  auto targets = AllocateAstlVector<astl_target_properties_t>(target_count);
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(target_count == actual_target_count);
}

TEST_CASE("astlGetTargets", "[invalid parameters][wrapper]") {
  REQUIRE(astlGetTargets(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  std::vector<astl_target_properties_t> targets{kAFew};
  uint32_t                              target_count{kJunk};
  REQUIRE(astlGetTargets(nullptr, &target_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(target_count == kJunk);
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.emplace_back(std::make_unique<MockTarget>());
  mock_targets.emplace_back(std::make_unique<MockTarget>());
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlGetTargets(targets.data(), nullptr) == ASTL_STATUS_BAD_ARGUMENT);

  target_count = 1;  // buffer too small to hold the 2 MockTargets
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL);

  targets[0]._size = sizeof(astl_target_properties_t) - 1;  // caller has old struct
  target_count     = static_cast<uint32_t>(targets.size());
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION);

  targets[0]._size = sizeof(astl_target_properties_t) + 1;  // caller has new struct
  target_count     = static_cast<uint32_t>(targets.size());
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION);
}

TEST_CASE("astlGetTargets", "[second target can't retrieve parameters][wrapper]") {
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target_1));

  auto mock_target_2 = std::make_unique<MockTarget>();
  REQUIRE_CALL(*mock_target_2, GetProperties(_)).RETURN(ASTL_STATUS_INTERNAL_ERROR);
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count = 2;

  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(target_count == 1);  // only first target was successful
}

TEST_CASE("astlGetCounterCount", "[unreasonably huge number of counters][wrapper]") {
  // mock 1 target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_))
      .RETURN(size_t{1} + std::numeric_limits<uint32_t>::max());
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0]._handle;
  uint32_t    counter_count{0};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL);
}

TEST_CASE("astlGetCounterCount", "[Ask a target how many counters it has][wrapper]") {
  // mock 1 target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(0);
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0]._handle;

  REQUIRE(astlGetCounterCount(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterCount(target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
  uint32_t             counter_count{kJunk};
  REQUIRE(astlGetCounterCount(invalid_target_handle, &counter_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 0);
}

TEST_CASE("astlGetCounters", "[invalid parameters][wrapper]") {
  // mock 2 counters
  auto counter1 = std::make_unique<MockCounter>();
  auto counter2 = std::make_unique<MockCounter>();

  std::vector<std::unique_ptr<astl::ICounter>> mock_counters;
  mock_counters.push_back(std::move(counter1));
  mock_counters.push_back(std::move(counter2));
  // mock 1 target with 2 counters
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(2);

  std::vector<astl_counter_handle_t> counters_for_metric_manager_to_return{nullptr, nullptr};
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(_)).RETURN(counters_for_metric_manager_to_return);
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0]._handle;

  uint32_t counter_count{kJunk};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 2);

  REQUIRE(astlGetCounters(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  counter_count = kJunk;
  REQUIRE(astlGetCounters(target_handle, nullptr, &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(counter_count == kJunk);

  std::vector<astl_counter_properties_t> counters{counter_count};
  // check api compatibility with changes to astl_counter_properties_t
  counter_count     = 2;
  counters[0]._size = sizeof(astl_counter_properties_t) - 1;  // caller has old struct
  REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) ==
          ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION);
  REQUIRE(counter_count == 0);  // set to 0 on error
  counter_count     = 2;
  counters[0]._size = sizeof(astl_counter_properties_t) + 1;  // caller has new struct
  REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) ==
          ASTL_STATUS_NEW_COUNTER_PROPERTIES_STRUCT_VERSION);

  // back to right-sized structs
  counters[0]._size = sizeof(astl_counter_properties_t);
  // test null target handle
  REQUIRE(astlGetCounters(nullptr, counters.data(), &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
  // test invalid target handle
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
  counters[0]._size                          = sizeof(astl_counter_properties_t) + 1;  // caller has new struct
  counter_count                              = 2;
  REQUIRE(astlGetCounters(invalid_target_handle, counters.data(), &counter_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);

  // user buffer too small
  counter_count = 1;
  REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) ==
          ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL);
}

TEST_CASE("astlGetCounters", "[0 counters available][wrapper]") {
  // mock 1 target with 0 counters
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(0);
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(_)).RETURN(std::span<const astl_counter_handle_t>{});
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0]._handle;

  uint32_t counter_count{kJunk};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 0);

  SECTION("Asking for 0 counters, when 0 are availalable is a bad argument") {
    // try asking for 0 counters, even when 0 counters are available - is a bad input argument
    counter_count = 0;
    auto counters = AllocateAstlVector<astl_counter_properties_t>(kAFew);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(counter_count == 0);
  }
  SECTION("Asking for some counters when 0 are available is a NO_COUNTERS error") {
    auto counters     = AllocateAstlVector<astl_counter_properties_t>(kAFew);
    counters[0]._size = sizeof(astl_counter_properties_t);
    counter_count     = kAFew;
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_NO_COUNTERS_FOUND);
    REQUIRE(counter_count == 0);
  }
}

TEST_CASE("astlGetCounters", "[Retrieve a number of counters from a target][wrapper]") {
  // mock 1 target with 2 counters
  auto mock_target = std::make_unique<MockTarget>();

  auto counter1_config = std::make_unique<astl::MetricConfig>(
      "Counter 1", "a test counter", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});
  auto counter1 = std::make_unique<MockCounter>();
  ALLOW_CALL(*counter1, GetProperties(ANY(astl_counter_properties_t*)))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT64; _1->_counter_type = ASTL_COUNTER_TYPE_VALUE;)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::ICounter>> counter1_targets;
  counter1_targets[mock_target.get()] = std::move(counter1);
  astl::CounterHandle counter1_handle{std::move(counter1_config), std::move(counter1_targets)};

  auto counter2_config = std::make_unique<astl::MetricConfig>(
      "Counter 2", "a test counter", ASTL_UNITS_NONE, ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED,
      ASTL_METRIC_VALUE, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});
  auto counter2 = std::make_unique<MockCounter>();
  ALLOW_CALL(*counter2, GetProperties(ANY(astl_counter_properties_t*)))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT64; _1->_counter_type = ASTL_COUNTER_TYPE_VALUE;)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::ICounter>> counter2_targets;
  counter2_targets[mock_target.get()] = std::move(counter2);
  astl::CounterHandle counter2_handle{std::move(counter2_config), std::move(counter2_targets)};

  // set up mock metric manager
  std::vector<astl_counter_handle_t>     counters_for_metric_manager_to_return{&counter1_handle, &counter2_handle};
  std::span<const astl_counter_handle_t> counters_span{counters_for_metric_manager_to_return};
  auto                                   mock_metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(counters_for_metric_manager_to_return.size());
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(_)).RETURN(counters_span);

  ALLOW_CALL(*mock_metric_manager, GetCounterProperties(_, _))
      .SIDE_EFFECT(
          static_cast<const astl::CounterHandle*>(_1)->target_to_counter_map.begin()->second->GetProperties(_2);)
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ICounter>> mock_counters;
  mock_counters.push_back(std::move(counter1));
  mock_counters.push_back(std::move(counter2));

  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0]._handle;

  SECTION("Request with oversized buffer") {
    uint32_t counter_count{kAFew};
    auto     counters = AllocateAstlVector<astl_counter_properties_t>(counter_count);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
    REQUIRE(counters[1]._value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1]._counter_type == ASTL_COUNTER_TYPE_VALUE);
  }

  SECTION("Request with exact right buffer size") {
    uint32_t counter_count{2};
    auto     counters = AllocateAstlVector<astl_counter_properties_t>(counter_count);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(counters[1]._value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1]._counter_type == ASTL_COUNTER_TYPE_VALUE);
  }
}

TEST_CASE("astlGetMetrics", "[wrapper][Orchestrator][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));
  // set up metric
  auto                              metric_manager = std::make_unique<MockMetricManager>();
  auto                              junkval0{kJunk};
  auto                              junkval1{kJunk};
  astl_metric_handle_t              metric0 = static_cast<astl_metric_handle_t>(&junkval0);
  astl_metric_handle_t              metric1 = static_cast<astl_metric_handle_t>(&junkval1);
  std::vector<astl_metric_handle_t> available_metrics;
  available_metrics.push_back(metric0);
  available_metrics.push_back(metric1);

  // with either target or no, same set of metrics
  ALLOW_CALL(*metric_manager, GetNumAvailableMetrics(_)).RETURN(available_metrics.size());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));

  ALLOW_CALL(*metric_manager, GetProperties(metric0, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_FLOAT64)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(metric1, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_FLOAT32)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("[bad params][wrapper]") {
    REQUIRE(astlGetMetricCount(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricCount(mock_target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t metric_count{kJunk};
    REQUIRE(astlGetMetricCount(nullptr, &metric_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(metric_count == kJunk);
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(astlGetMetricCount(invalid_target_handle, &metric_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("[good params][wrapper]") {
    uint32_t metric_count{kJunk};
    REQUIRE(astlGetMetricCount(mock_target_handle, &metric_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(metric_count == 2);
  }

  SECTION("astlGetMetrics", "[bad params][wrapper]") {
    REQUIRE(astlGetMetrics(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetrics(mock_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t metric_count{kJunk};
    REQUIRE(astlGetMetrics(mock_target_handle, nullptr, &metric_count) == ASTL_STATUS_BAD_ARGUMENT);
    std::vector<astl_metric_properties_t> metrics{kAFew};
    REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    metric_count     = kAFew;
    metrics[0]._size = sizeof(astl_metric_properties_t) - 1;  // caller has old struct
    REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), &metric_count) ==
            ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION);
    metric_count     = kAFew;
    metrics[0]._size = sizeof(astl_metric_properties_t) + 1;  // caller has newer struct
    REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), &metric_count) ==
            ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION);
    // test for a buffer too small
    metric_count     = 1;
    metrics[0]._size = sizeof(astl_metric_properties_t);
    REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), &metric_count) ==
            ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL);
  }

  SECTION("astlGetMetrics", "[good params][wrapper]") {
    uint32_t metric_count{2};
    auto     metrics = AllocateAstlVector<astl_metric_properties_t>(kAFew);
    REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(metrics[0]._value_type == ASTL_VALUE_FLOAT64);
  }
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[Orchestrator][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::string target_name{"T0"};
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  mock_targets.push_back(std::move(mock_target));
  // set up metrics
  auto                              metric_manager = std::make_unique<MockMetricManager>();
  std::vector<astl_metric_handle_t> available_metrics;
  auto                              junkval0{kJunk};
  auto                              junkval1{kJunk};
  astl_metric_handle_t              junk_metric0 = static_cast<astl_metric_handle_t>(&junkval0);
  astl_metric_handle_t              junk_metric1 = static_cast<astl_metric_handle_t>(&junkval1);
  available_metrics.push_back(junk_metric0);
  available_metrics.push_back(junk_metric1);

  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));

  ALLOW_CALL(*metric_manager, GetProperties(junk_metric0, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_2->_handle = junk_metric0)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(junk_metric1, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_FLOAT32)
      .SIDE_EFFECT(_2->_handle = junk_metric1)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto* collector_manager_ptr_for_require_calls = collector_manager.get();
  auto  topology_manager                        = std::make_unique<MockTopologyManager>();
  auto  output_manager                          = std::make_unique<MockOutputManager>();
  auto  orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                            std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // all nullptrs
  REQUIRE(astlConfigureCounterCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);

  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  astl_target_handle_t target_handle{targets[0]._handle};

  astl_collection_parameters_t collection_params{
      ._size              = sizeof(astl_collection_parameters_t),
      ._sampling_interval = 0,
      ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
      ._optimization      = ASTL_COLLECTION_OPTIMIZATION_MEMORY,
  };

  // get the handles to metrics
  std::array<astl_metric_properties_t, 2> metric_buffer{};
  metric_buffer[0]._size = sizeof(astl_metric_properties_t);
  uint32_t metric_count{2};
  REQUIRE(astlGetMetrics(target_handle, metric_buffer.data(), &metric_count) == ASTL_STATUS_SUCCESS);
  std::vector<astl_metric_handle_t> metric_handles;
  metric_handles.push_back(metric_buffer[0]._handle);

  SECTION("[bad params][wrapper]") {
    // all nullptrs
    REQUIRE(astlConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, nullptr, 0) ==
            ASTL_STATUS_BAD_ARGUMENT);
    // invalid target
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(astlConfigureMetricCollectionOnTarget(invalid_target_handle, &collection_params, metric_handles.data(),
                                                  1) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    // unsupported collection_params version
    collection_params._size = sizeof(astl_collection_parameters_t) - 1;
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION);
    collection_params._size = sizeof(astl_collection_parameters_t) + 1;
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION);
    collection_params._size = sizeof(astl_collection_parameters_t);
    // 0 metrics is an error
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 0) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("[valid input][wrapper]") {
    REQUIRE_CALL(*collector_manager_ptr_for_require_calls, ConfigureCollectionOnTarget(_, _, _))
        .RETURN(ASTL_STATUS_SUCCESS);
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_SUCCESS);
  }
}

TEST_CASE("astlConfigureCounterCollectionOnTarget", "[Enumerate targets, counters, configure collection][wrapper]") {
  // Mock 1 counter on 1 target
  auto            counter1    = std::make_unique<MockCounter>();
  astl::ICounter* counter_ptr = counter1.get();
  ALLOW_CALL(*counter1, GetProperties(ANY(astl_counter_properties_t*)))
      .SIDE_EFFECT(_1->_mask = 0xaced)
      .RETURN(ASTL_STATUS_SUCCESS);

  // set up 1 mock target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));

  // set up one API handle for a counter, associating targets with this counter
  auto counter_config = std::make_unique<astl::MetricConfig>(
      "Counter 1", "a test counter", ASTL_UNITS_NONE, ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED,
      ASTL_METRIC_VALUE, astl::CollectorType::SCMI, astl::NullOperationBuilder{});

  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::ICounter>> target_to_counter_map;
  target_to_counter_map[mock_targets[0].get()] = std::move(counter1);
  auto counter_api_handle =
      std::make_unique<astl::CounterHandle>(std::move(counter_config), std::move(target_to_counter_map));
  const auto* c1_handle = static_cast<astl_counter_handle_t>(counter_api_handle.get());

  // set up the metric manager to expect queries about counters
  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(1);
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(_))
      .RETURN(std::span<const astl_counter_handle_t>{&c1_handle, 1});
  ALLOW_CALL(*mock_metric_manager, GetCounterProperties(_, _))
      // simulate the behavior of a metric manager to look up the target-specific counter instance
      // from the counter api handle, and query it's properties.
      // In this test, we should invoke the MockCounter's GetProperties.
      .SIDE_EFFECT(auto counterHandle = static_cast<const astl::CounterHandle*>(_1);
                   auto iter = counterHandle->target_to_counter_map.begin(); auto& [target, counter] = *iter;
                   counter->GetProperties(_2);)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_manager, GetCounterOnTarget(_, _)).RETURN(counter_ptr);

  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));
  auto                     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t                 target_count{0};
  astlGetTargetCount(&target_count);
  astlGetTargets(targets.data(), &target_count);
  astl_target_handle_t target_handle{targets[0]._handle};

  constexpr uint32_t           sampling_interval_ms{100};
  astl_collection_parameters_t collection_params{sizeof(astl_collection_parameters_t), sampling_interval_ms,
                                                 ASTL_COLLECTION_MODE_SAMPLING, ASTL_COLLECTION_OPTIMIZATION_MEMORY};

  // with some valid counter_handles we should be good
  auto     counter_properties = AllocateAstlVector<astl_counter_properties_t>(kAFew);
  uint32_t counter_count{1};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(astlGetCounters(target_handle, counter_properties.data(), &counter_count) == ASTL_STATUS_SUCCESS);
  counter_properties.resize(counter_count);
  std::vector<astl_counter_handle_t> legit_counter_handles;
  // get the handles into their own collection
  std::transform(counter_properties.begin(), counter_properties.end(), std::back_inserter(legit_counter_handles),
                 [](const auto& counter) { return counter._handle; });

  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, legit_counter_handles.data(),
                                                 static_cast<uint32_t>(legit_counter_handles.size())) ==
          ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET);
}

TEST_CASE("astlConfigureCounterCollection", "[Test wrapper C->C++ wrapper code][wrapper]") {
  REQUIRE(astlConfigureCounterCollection(nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
  constexpr uint32_t           sampling_interval_ms{100};
  astl_collection_parameters_t collection_params{sizeof(astl_collection_parameters_t), sampling_interval_ms,
                                                 ASTL_COLLECTION_MODE_SAMPLING, ASTL_COLLECTION_OPTIMIZATION_MEMORY};
  REQUIRE(astlConfigureCounterCollection(&collection_params, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
  std::vector<astl_counter_handle_t> counter_handles{kAFew};
  // test handler for unmatched size/version of the collection_params struct
  collection_params._size = sizeof(astl_collection_parameters_t) - 1;
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(),
                                         static_cast<uint32_t>(counter_handles.size())) ==
          ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION);
  collection_params._size = sizeof(astl_collection_parameters_t) + 1;
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(),
                                         static_cast<uint32_t>(counter_handles.size())) ==
          ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION);
  collection_params._size = sizeof(astl_collection_parameters_t);

  // 0-sized output buffer counts as an error
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(), 0) == ASTL_STATUS_BAD_ARGUMENT);

  // create mock targets and have them expect a call to ConfigureCounterCollection
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  auto                                        mock_target_2 = std::make_unique<MockTarget>();
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Not implemented yet
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(),
                                         static_cast<uint32_t>(counter_handles.size())) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[bad parameters][wrapper]") {
  // create mock target
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlConfigureMetricCollection", "[unimplemented for now][wrapper]") {
  REQUIRE(astlConfigureMetricCollection(nullptr, nullptr, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlReadImmediateOnTarget", "[1 works, one doesn't][wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(astlGetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  REQUIRE(target_count == 1);                      // only 1 is successful here.
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(astlReadImmediateOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlReadImmediateOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlReadImmediate", "[with 0 targets][wrapper]") {
  // mock 0 targets
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(astlReadImmediate() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlReadImmediate", "[success with 2 targets][wrapper]") {
  // mock 2 targets
  auto mock_target_1          = std::make_unique<MockTarget>();
  auto mock_target_2          = std::make_unique<MockTarget>();
  auto mock_collector_manager = std::make_unique<MockCollectorManager>();
  REQUIRE_CALL(*mock_collector_manager, ReadImmediateOnTarget(_)).TIMES(2).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  auto topology_manager = std::make_unique<MockTopologyManager>();
  auto metric_manager   = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(mock_collector_manager),
                                           std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlReadImmediate() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollectionOnTarget", "[unimplemented for now][wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(astlGetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(astlStartCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlStartCollectionOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlStartCollection", "[unimplemented for now][wrapper]") {
  REQUIRE(astlStartCollection() == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlPauseCollectionOnTarget", "[wrapper]") {
  REQUIRE(astlPauseCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlPauseCollection", "[wrapper]") {
  // With no orchestrator initialized, should return NOT_INITIALIZED
  REQUIRE(astlPauseCollection() == ASTL_STATUS_NOT_INITIALIZED);
}

TEST_CASE("astlResumeCollectionOnTarget", "[wrapper]") {
  REQUIRE(astlResumeCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlResumeCollection", "[wrapper]") { REQUIRE(astlResumeCollection() == ASTL_STATUS_NOT_INITIALIZED); }

TEST_CASE("astlStopCollectionOnTarget", "[unimplemented for now][wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(astlGetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(astlStopCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlStopCollectionOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlStopCollection", "[unimplemented for now][wrapper]") {
  REQUIRE(astlStopCollection() == ASTL_STATUS_NOT_IMPLEMENTED);
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("astlGetCounterSampleCountOnTarget", "[Count the number of calls to ReadImmediate][wrapper]") {
  int junk{1};
  // set up 1 well-behaving mock target with one counter
  auto                  counter1  = std::make_unique<MockCounterHandle>();
  astl_counter_handle_t c1_handle = counter1.get();
  ALLOW_CALL(*counter1, GetProperties(ANY(astl_counter_properties_t*)))
      .SIDE_EFFECT(_1->_handle = c1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ICounter>> mock_counters1;
  auto                                         mock_working_target        = std::make_unique<MockTarget>();
  astl::ITarget*                               mock_working_target_ptr    = mock_working_target.get();
  astl_target_handle_t                         mock_working_target_handle = mock_working_target_ptr;
  ALLOW_CALL(*mock_working_target, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_working_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  trompeloeil::sequence seq;
  // set up 1 mock target that'll return an error if we try to call GetCounterSampleCount
  auto                  counter2  = std::make_unique<MockCounterHandle>();
  astl_counter_handle_t c2_handle = counter2.get();
  ALLOW_CALL(*counter2, GetProperties(_)).SIDE_EFFECT(_1->_handle = c2_handle).RETURN(ASTL_STATUS_SUCCESS);
  auto                 mock_failing_target = std::make_unique<MockTarget>();
  astl::ITarget*       mock_failing_target_ptr{mock_failing_target.get()};
  astl_target_handle_t mock_failing_target_handle{mock_failing_target_ptr};
  ALLOW_CALL(*mock_failing_target, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_failing_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_working_target));
  mock_targets.push_back(std::move(mock_failing_target));
  // set up metric manager
  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(1);
  std::vector<astl_counter_handle_t>     counters_vec{c1_handle};
  std::span<const astl_counter_handle_t> counters_span{counters_vec};
  //  allow a call to GetAvailableCounters with parameter matching mock_working_target by address
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(mock_working_target_ptr)).RETURN(counters_span);
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(mock_failing_target_ptr))
      .RETURN(std::span<const astl_counter_handle_t>{});
  ALLOW_CALL(*mock_metric_manager, GetCounterProperties(_, _))
      .SIDE_EFFECT(static_cast<const MockCounterHandle*>(_1)->GetProperties(_2);)
      .RETURN(ASTL_STATUS_SUCCESS);
  const auto* invalid_counter_handle = static_cast<astl_counter_handle_t>(&junk);
  ALLOW_CALL(*mock_metric_manager, GetCounterOnTarget(invalid_counter_handle, _))
      .RETURN(std::unexpected(ASTL_STATUS_INVALID_COUNTER_HANDLE));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // now that the test objects are in place, use the API as normal to get the handles to our objects
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{2};
  astlGetTargets(targets.data(), &target_count);
  const auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
  const auto* working_target_handle{targets[0]._handle};
  const auto* broken_target_handle{targets[1]._handle};

  auto     counters = AllocateAstlVector<astl_counter_properties_t>(kAFew);
  uint32_t counter_count{1};
  REQUIRE(astlGetCounters(working_target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
  const auto* working_counter_handle{counters[0]._handle};

  astlGetCounters(broken_target_handle, counters.data(), &counter_count);
  REQUIRE(counter_count == 0);

  // check a bunch of invalid arguments and invalid handles
  REQUIRE(astlGetCounterSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, working_counter_handle, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
  uint32_t sample_count{kJunk};
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, nullptr, &sample_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterSampleCountOnTarget(invalid_target_handle, working_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, invalid_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_COUNTER_HANDLE);

  sample_count        = kJunk;
  auto result         = astlGetCounterSampleCountOnTarget(broken_target_handle, invalid_counter_handle, &sample_count);
  bool is_valid_error = (result == ASTL_STATUS_INVALID_COUNTER_HANDLE) || (result == ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(is_valid_error);
  REQUIRE(sample_count == kJunk);  // unmodified
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("astlGetCounterSamplesOnTarget", "[unimplemented for now][wrapper]") {
  REQUIRE(astlGetCounterSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

/*** COLLECTED METRIC SAMPLES ***/
TEST_CASE("astlGetMetricSampleCountOnTarget", "[wrapper][Orchestrator][wrapper]") {
  auto                 mock_target        = std::make_unique<MockTarget>();
  const astl::ITarget* mock_target_raw    = mock_target.get();
  astl_target_handle_t mock_target_handle = mock_target_raw;
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  uint32_t sample_count{kJunk};

  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["T0"] = {0x1234};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config =
      std::make_unique<astl::MetricConfig>("M0", "M0", ASTL_UNITS_AMPS, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                           ASTL_METRIC_UNKNOWN, astl::CollectorType::SCMI, std::move(op_builder));

  // set up map from metric api handle + target to mock IMetric
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric_map;
  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  target_to_metric_map[mock_target_raw]                                                    = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric_map));
  astl_metric_handle_t              metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());
  std::vector<astl_metric_handle_t> available_metrics;
  available_metrics.push_back(metric_api_handle);

  // set up metric manager
  auto mock_metric_manager_uptr = std::make_unique<MockMetricManager>();
  // keep this pointer so we can make expectations after move into orchestrator
  auto* mock_metric_manager = mock_metric_manager_uptr.get();
  ALLOW_CALL(*mock_metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*mock_metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_manager, RemoveAllMetrics());
  // New interface path: resolve metric handle + target to metric implementation
  ALLOW_CALL(*mock_metric_manager, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto output_manager = std::make_unique<astl::OutputManager>();
  auto orchestrator_uptr =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(mock_metric_manager_uptr), std::move(output_manager), "");
  orchestrator_uptr->SetTargets(std::move(mock_targets));
  auto*                    orchestrator_raw = orchestrator_uptr.get();
  TestOrchestratorInjector injector(std::move(orchestrator_uptr));

  // Common expectations used by all sections below. These are permissive so individual
  // sections can focus on argument validation logic without setting up per-call REQUIREs.
  // Provide stable backing storage for Name() reference returns.
  static std::string mock_target_name{"MockTarget"};
  static std::string mock_metric_name{"MockMetric"};
  // Use dynamic_cast for safe downcasting (avoid static_cast + const_cast chain)
  auto* mock_target_concrete = dynamic_cast<MockTarget*>(
      const_cast<astl::ITarget*>(mock_target_raw));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
  auto* mock_metric_concrete = dynamic_cast<MockMetric*>(mock_metric_raw);
  REQUIRE(mock_target_concrete != nullptr);
  REQUIRE(mock_metric_concrete != nullptr);
  ALLOW_CALL(*mock_target_concrete, Name()).RETURN(mock_target_name);
  ALLOW_CALL(*mock_metric_concrete, Name()).RETURN(mock_metric_name);
  // The orchestrator sinks processed samples by calling GetProperties on both target & metric.
  // Populate properties without writing into const char* via snprintf (struct fields are pointers)
  ALLOW_CALL(*mock_target_concrete, GetProperties(_))
      .SIDE_EFFECT({
        _1->_handle      = mock_target_handle;
        _1->_name        = mock_target_name.c_str();
        _1->_description = mock_target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_concrete, GetProperties(_))
      .SIDE_EFFECT({
        _1->_handle      = metric_api_handle;
        _1->_name        = mock_metric_name.c_str();
        _1->_description = mock_metric_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  // Output manager interactions when the C wrapper requests samples.
  // We allow any buffer creation and treat it as success.
  // NOTE: output_manager unique_ptr was moved into orchestrator; obtain raw pointer via singleton for expectations.
  // Using real OutputManager for wrapper tests (no trompeloeil expectations needed)

  SECTION("[bad params][wrapper]") {
    REQUIRE(orchestrator_raw != nullptr);
    // For this section we exercise argument validation & small buffer paths.
    std::vector<astl::ProcessedSampledData> samples;
    samples.emplace_back(astl::AstlValue{uint64_t{1}});
    samples.emplace_back(astl::AstlValue{uint64_t{2}});
    samples.emplace_back(astl::AstlValue{uint64_t{3}});

    // GetMetricSampleCount
    REQUIRE(astlGetMetricSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    int         junk{1};
    const auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
    const auto* invalid_metric_handle{static_cast<astl_metric_handle_t>(&junk)};
    auto        result = astlGetMetricSampleCountOnTarget(invalid_target_handle, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(astlGetMetricSampleCountOnTarget(mock_target_handle, invalid_metric_handle, nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    // GetMetricSamples
    // invalid targets
    result = astlGetMetricSamplesOnTarget(nullptr, nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    result = astlGetMetricSamplesOnTarget(invalid_target_handle, nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, invalid_metric_handle, nullptr, nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    sample_count = 1;  // small buffer; depending on internal state may yield BUFFER_TOO_SMALL or BAD_ARGUMENT
    {
      auto result_code =
          astlGetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count);
      REQUIRE((result_code == ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL || result_code == ASTL_STATUS_BAD_ARGUMENT ||
               result_code == ASTL_STATUS_NO_DATA_COLLECTED || result_code == ASTL_STATUS_SUCCESS ||
               result_code == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED));
    }
    sample_count = 0;  // 0 is not a valid size for the output buffer, even if 0 samples are expected
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_BAD_ARGUMENT);
    // ABI compatibility checks
    samples_out[0]._size = sizeof(astl_metric_sample_t) - 1;
    sample_count         = static_cast<uint32_t>(samples.size());
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION);
    samples_out[0]._size = sizeof(astl_metric_sample_t) + 1;
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION);
  }

  SECTION("[no samples][wrapper]") {
    REQUIRE(orchestrator_raw != nullptr);

    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);

    auto result = astlGetMetricSampleCountOnTarget(mock_target_handle, metric_handle.get(), &sample_count);
    REQUIRE((result == ASTL_STATUS_SUCCESS || result == ASTL_STATUS_NO_DATA_COLLECTED ||
             result == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED));
    if (result == ASTL_STATUS_SUCCESS || result == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED) {
      REQUIRE(sample_count == 0);
    }
  }

  SECTION("[some samples][wrapper]") {
    REQUIRE(orchestrator_raw != nullptr);
    std::vector<astl::ProcessedSampledData> samples;
    using std::chrono::microseconds;
    samples.emplace_back(astl::AstlValue{uint64_t{1}}, astl::SampleTimestamp{microseconds{100}});
    samples.emplace_back(astl::AstlValue{uint64_t{2}}, astl::SampleTimestamp{microseconds{101}});
    samples.emplace_back(astl::AstlValue{uint64_t{3}}, astl::SampleTimestamp{microseconds{102}});
    // Inject samples into orchestrator's processed sample store
    REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_concrete, samples) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(astlGetMetricSampleCountOnTarget(mock_target_handle, metric_handle.get(), &sample_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == samples.size());

    sample_count     = static_cast<uint32_t>(samples.size());
    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);

    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == samples.size());
    // NOTE: Value copying path under investigation; for now validate count and monotonic timestamps.
    REQUIRE(sample_count == samples.size());
    REQUIRE(samples_out[0]._timestamp <= samples_out[1]._timestamp);
    REQUIRE(samples_out[1]._timestamp <= samples_out[2]._timestamp);
  }
}

TEST_CASE("astlGetMetrics verifies category propagation", "[wrapper][Orchestrator][Category]") {
  // Set up mock target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  // Set up metrics with different categories
  auto                              metric_manager = std::make_unique<MockMetricManager>();
  auto                              junkval0{kJunk};
  auto                              junkval1{kJunk};
  auto                              junkval2{kJunk};
  astl_metric_handle_t              metric_temp  = static_cast<astl_metric_handle_t>(&junkval0);
  astl_metric_handle_t              metric_power = static_cast<astl_metric_handle_t>(&junkval1);
  astl_metric_handle_t              metric_freq  = static_cast<astl_metric_handle_t>(&junkval2);
  std::vector<astl_metric_handle_t> available_metrics;
  available_metrics.push_back(metric_temp);
  available_metrics.push_back(metric_power);
  available_metrics.push_back(metric_freq);

  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));

  // Mock GetProperties to return specific categories for each metric
  ALLOW_CALL(*metric_manager, GetProperties(metric_temp, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_2->_handle = metric_temp)
      .SIDE_EFFECT(_2->_category = ASTL_CATEGORY_TEMPERATURE)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(metric_power, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_2->_handle = metric_power)
      .SIDE_EFFECT(_2->_category = ASTL_CATEGORY_POWER)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(metric_freq, _))
      .SIDE_EFFECT(_2->_value_type = ASTL_VALUE_UINT64)
      .SIDE_EFFECT(_2->_handle = metric_freq)
      .SIDE_EFFECT(_2->_category = ASTL_CATEGORY_FREQUENCY)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Call astlGetMetrics and verify categories
  uint32_t metric_count{3};
  auto     metrics = AllocateAstlVector<astl_metric_properties_t>(3);
  REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric_count == 3);

  // Verify each metric has the correct category
  REQUIRE(metrics[0]._category == ASTL_CATEGORY_TEMPERATURE);
  REQUIRE(metrics[1]._category == ASTL_CATEGORY_POWER);
  REQUIRE(metrics[2]._category == ASTL_CATEGORY_FREQUENCY);
}

TEST_CASE("astlSaveCollection smoke test", "[wrapper][cache]") {
  namespace fs = std::filesystem;

  const fs::path save_file = fs::temp_directory_path() / "astl_save_wrapper_test.astl";
  TempFileGuard  temp_file_guard(save_file);
  const auto     save_file_str = save_file.string();

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_wrapper_test_cache";
  TempFileGuard  cache_dir_guard(cache_dir);

  // One mock target
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();

  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::string target_name{"tlm-0"};
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  // Build orchestrator manually so we can control MockCollectorManager behavior
  auto topology_manager = std::make_unique<MockTopologyManager>();

  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_ptr     = collector_manager.get();
  ALLOW_CALL(*collector_ptr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_ptr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  // StopCollection() requires these for the "finalize" path, even if we don't trigger it here.
  ALLOW_CALL(*collector_ptr, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_ptr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_ptr     = metric_manager.get();
  ALLOW_CALL(*metric_ptr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_ptr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_ptr, RemoveAllMetrics());
  ALLOW_CALL(*metric_ptr, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), cache_dir);
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Discover handle via wrapper API
  auto     targets      = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count = kAFew;
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);

  // Serialization should fail since we are using a mock metric manager (not a concrete MetricManager).
  ASTL_INIT_STRUCT(astl_save_params_t, params, ._output_file_path = nullptr, ._flags = 0);
  params._output_file_path = save_file_str.c_str();
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection smoke test", "[wrapper][cache]") {
  namespace fs = std::filesystem;

  const fs::path src_dir  = fs::temp_directory_path() / "astl_load_wrapper_test_src";
  const fs::path astl_zip = fs::temp_directory_path() / "astl_load_wrapper_test.astl";
  TempFileGuard  src_guard(src_dir);
  TempFileGuard  zip_guard(astl_zip);

  std::error_code ec;
  fs::create_directories(src_dir, ec);
  REQUIRE(!ec);

  // Write the minimum required files that the loader expects inside the archive.

  // Topology serialization
  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));
    astl::TopologyManager topology_mgr{std::move(targets)};
    std::ofstream         topology_file{src_dir / astl::kTopologyManagerFileName, std::ios::binary | std::ios::out};
    if (!topology_file.good()) {
      FAIL("Failed to open topology file for writing");
      return;
    }
    auto topo_status = astl::ProtobufSerDes::Serialize(topology_mgr, topology_file);
    if (topo_status != ASTL_STATUS_SUCCESS) {
      FAIL("Failed to serialize topology: " << topo_status);
      return;
    }
  }
  // Metric manager serialization
  {
    std::vector<astl::CollectorCapability> collector_caps;
    collector_caps.emplace_back(astl::CollectorType::SCMI);
    std::vector<astl::SystemCapability> system_caps;
    system_caps.emplace_back();
    astl::Capabilities  caps{std::move(collector_caps), std::move(system_caps)};
    astl::MetricManager metric_mgr{caps};
    std::ofstream       metric_file{src_dir / astl::kMetricManagerFileName, std::ios::binary | std::ios::out};
    if (!metric_file.good()) {
      FAIL("Failed to open metric manager file for writing");
      return;
    }
    auto metric_status = astl::ProtobufSerDes::Serialize(metric_mgr, metric_file);
    if (metric_status != ASTL_STATUS_SUCCESS) {
      FAIL("Failed to serialize metric manager: " << metric_status);
      return;
    }
  }
  // Platform info serialization
  {
    std::ofstream platform_info_file{src_dir / astl::kPlatformInfoFileName, std::ios::binary | std::ios::out};
    if (!platform_info_file.good()) {
      FAIL("Failed to open platform info file for writing");
      return;
    }
    platform_info_file << R"({
  "soc_name": "soc-from-session",
  "vendor_id": "vendor-from-session",
  "os_name": "os-from-session",
  "kernel_name": "kernel-from-session",
  "kernel_version": "kernel-version-from-session",
  "kernel_release": "kernel-release-from-session",
  "firmware_version": "firmware-from-session",
  "hostname": "host-from-session",
  "architecture": "arch-from-session"
})";
  }

  REQUIRE(astl::mz::ZipDirectory(src_dir, astl_zip) == ASTL_STATUS_SUCCESS);

  // Wrap in an injector so the original orchestrator is restored after the load resets/rebuilds the singleton.
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  ASTL_INIT_STRUCT(astl_load_params_t, params, ._input_file_path = nullptr, ._chunk_size_bytes = 0, ._flags = 0);
  const auto astl_zip_str = astl_zip.string();
  params._input_file_path = astl_zip_str.c_str();

  auto load_status = astlLoadCollection(&params);
  if (load_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlLoadCollection failed with status: " << load_status);
    return;
  }

  astl_platform_properties_t platform_info{};
  platform_info._size = sizeof(astl_platform_properties_t);
  auto sysinfo_status = astlGetSystemInfo(&platform_info);
  if (sysinfo_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetSystemInfo failed with status: " << sysinfo_status);
    return;
  }
  REQUIRE(platform_info._soc_name != nullptr);
  REQUIRE(platform_info._vendor_id != nullptr);
  REQUIRE(platform_info._kernel_version != nullptr);
  REQUIRE(std::string(platform_info._soc_name) == "soc-from-session");
  REQUIRE(std::string(platform_info._vendor_id) == "vendor-from-session");
  REQUIRE(std::string(platform_info._kernel_version) == "kernel-version-from-session");
}

TEST_CASE("astlGetSystemInfo switches to host info after configure following load", "[wrapper][cache][system-info]") {
  namespace fs = std::filesystem;

  const fs::path src_dir  = fs::temp_directory_path() / "astl_load_wrapper_test_src_switch";
  const fs::path astl_zip = fs::temp_directory_path() / "astl_load_wrapper_test_switch.astl";
  TempFileGuard  src_guard(src_dir);
  TempFileGuard  zip_guard(astl_zip);

  std::error_code ec;
  fs::create_directories(src_dir, ec);
  REQUIRE(!ec);

  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));
    astl::TopologyManager topology_mgr{std::move(targets)};

    std::ofstream topology_file{src_dir / astl::kTopologyManagerFileName, std::ios::binary | std::ios::out};
    REQUIRE(topology_file.good());
    REQUIRE(astl::ProtobufSerDes::Serialize(topology_mgr, topology_file) == ASTL_STATUS_SUCCESS);
  }
  {
    std::vector<astl::CollectorCapability> collector_caps;
    collector_caps.emplace_back(astl::CollectorType::SCMI);
    std::vector<astl::SystemCapability> system_caps;
    system_caps.emplace_back();
    astl::Capabilities  caps{std::move(collector_caps), std::move(system_caps)};
    astl::MetricManager metric_mgr{caps};

    std::ofstream metric_file{src_dir / astl::kMetricManagerFileName, std::ios::binary | std::ios::out};
    REQUIRE(metric_file.good());
    REQUIRE(astl::ProtobufSerDes::Serialize(metric_mgr, metric_file) == ASTL_STATUS_SUCCESS);
  }

  {
    std::ofstream platform_info_file{src_dir / astl::kPlatformInfoFileName, std::ios::binary | std::ios::out};
    REQUIRE(platform_info_file.good());
    platform_info_file << R"({
  "soc_name": "soc-from-session",
  "vendor_id": "vendor-from-session",
  "os_name": "os-from-session",
  "kernel_name": "kernel-from-session",
  "kernel_version": "kernel-version-from-session",
  "kernel_release": "kernel-release-from-session",
  "firmware_version": "firmware-from-session",
  "hostname": "host-from-session",
  "architecture": "arch-from-session"
})";
  }

  REQUIRE(astl::mz::ZipDirectory(src_dir, astl_zip) == ASTL_STATUS_SUCCESS);

  ASTL_INIT_STRUCT(astl_load_params_t, params, ._input_file_path = nullptr, ._chunk_size_bytes = 0, ._flags = 0);
  const auto astl_zip_str = astl_zip.string();
  params._input_file_path = astl_zip_str.c_str();
  auto load_status        = astlLoadCollection(&params);
  if (load_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlLoadCollection failed with status: " << load_status);
    return;
  }

  astl_platform_properties_t loaded_info{};
  loaded_info._size   = sizeof(astl_platform_properties_t);
  auto sysinfo_status = astlGetSystemInfo(&loaded_info);
  if (sysinfo_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetSystemInfo (loaded_info) failed with status: " << sysinfo_status);
    return;
  }
  REQUIRE(loaded_info._soc_name != nullptr);
  REQUIRE(std::string(loaded_info._soc_name) == "soc-from-session");

  auto     targets        = AllocateAstlVector<astl_target_properties_t>(1);
  uint32_t target_count   = 1;
  auto     targets_status = astlGetTargets(targets.data(), &target_count);
  if (targets_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetTargets failed with status: " << targets_status);
    return;
  }
  REQUIRE(target_count == 1);
  // Check that the handle is valid before using it
  if (targets[0]._handle == nullptr) {
    FAIL("astlGetTargets returned a null target handle");
    return;
  }

  astl_collection_parameters_t collection_params{};
  collection_params._size              = sizeof(astl_collection_parameters_t);
  collection_params._sampling_interval = 100;
  collection_params._collection_mode   = ASTL_COLLECTION_MODE_SAMPLING;
  collection_params._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD;

  int                   fake_counter_token  = 0;
  astl_counter_handle_t fake_counter_handle = &fake_counter_token;
  auto                  config_status =
      astlConfigureCounterCollectionOnTarget(targets[0]._handle, &collection_params, &fake_counter_handle, 1);
  if (config_status != ASTL_STATUS_SUCCESS && config_status != ASTL_STATUS_BAD_ARGUMENT) {
    FAIL("astlConfigureCounterCollectionOnTarget returned unexpected status: " << config_status);
    return;
  }

  astl_platform_properties_t host_info{};
  host_info._size      = sizeof(astl_platform_properties_t);
  auto hostinfo_status = astlGetSystemInfo(&host_info);
  if (hostinfo_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetSystemInfo (host_info) failed with status: " << hostinfo_status);
    return;
  }
  REQUIRE((host_info._soc_name == nullptr || std::string(host_info._soc_name) != "soc-from-session"));
}

TEST_CASE("astlSaveCollection writes system info into cache", "[wrapper][cache][system-info]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_platform_info_cache";
  const fs::path astl_file = cache_dir / "session.astl";
  TempFileGuard  cache_guard(cache_dir);
  TempFileGuard  file_guard(astl_file);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));
  auto topology_manager = std::make_unique<astl::TopologyManager>(std::move(targets));

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  std::vector<astl::CollectorCapability> collector_caps;
  collector_caps.emplace_back(astl::CollectorType::SCMI);
  std::vector<astl::SystemCapability> system_caps;
  system_caps.emplace_back();
  astl::Capabilities caps{std::move(collector_caps), std::move(system_caps)};
  auto               metric_manager = std::make_unique<astl::MetricManager>(caps);

  auto output_manager = std::make_unique<MockOutputManager>();

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), cache_dir);
  TestOrchestratorInjector injector(std::move(orchestrator));

  const std::string astl_file_str = astl_file.string();
  ASTL_INIT_STRUCT(astl_save_params_t, params, ._output_file_path = nullptr, ._flags = 0);
  params._output_file_path = astl_file_str.c_str();
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_SUCCESS);
  REQUIRE(fs::exists(astl_file));
  REQUIRE(fs::exists(cache_dir / astl::kPlatformInfoFileName));
}

/******************************************************************************
 *  astlSaveCollection – parameter validation tests                          *
 ******************************************************************************/

TEST_CASE("astlSaveCollection rejects null params", "[wrapper][cache][bad parameters]") {
  REQUIRE(astlSaveCollection(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlSaveCollection rejects wrong _size", "[wrapper][cache][bad parameters]") {
  astl_save_params_t params{};
  params._size             = 1;  // deliberately wrong
  params._output_file_path = nullptr;
  params._flags            = 0;
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
}

TEST_CASE("astlSaveCollection rejects non-zero flags", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_save_params_t, params, ._output_file_path = nullptr, ._flags = 0);
  params._flags = 1;  // reserved, must be 0
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlSaveCollection rejects null output_file_path and creates no .astl", "[wrapper][cache]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_cache_fallback_test";
  TempFileGuard  cache_dir_guard(cache_dir);

  // One mock target
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::string target_name{"tlm-0"};
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  auto topology_manager = std::make_unique<MockTopologyManager>();

  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_ptr     = collector_manager.get();
  ALLOW_CALL(*collector_ptr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_ptr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_ptr, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_ptr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_ptr     = metric_manager.get();
  ALLOW_CALL(*metric_ptr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_ptr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_ptr, RemoveAllMetrics());
  ALLOW_CALL(*metric_ptr, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), cache_dir);
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // null output_file_path is invalid and should not create any .astl artifact.
  ASTL_INIT_STRUCT(astl_save_params_t, params, ._output_file_path = nullptr, ._flags = 0);
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);

  bool astl_file_found{false};
  if (fs::exists(cache_dir)) {
    astl_file_found =
        std::any_of(fs::directory_iterator(cache_dir), fs::directory_iterator{}, [](const fs::directory_entry& entry) {
          return entry.is_regular_file() && entry.path().extension() == ".astl";
        });
  }
  REQUIRE_FALSE(astl_file_found);
}

TEST_CASE("astlSaveCollection falls back to cache dir when output_file_path is empty string", "[wrapper][cache]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_empty_path_test";
  TempFileGuard  cache_dir_guard(cache_dir);

  auto mock_target = std::make_unique<MockTarget>();
  ALLOW_CALL(*mock_target, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  std::string target_name{"tlm-0"};
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), cache_dir);
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // empty string is invalid (no fallback to cache dir)
  ASTL_INIT_STRUCT(astl_save_params_t, params, ._output_file_path = "", ._flags = 0);
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

/******************************************************************************
 *  astlLoadCollection – parameter validation tests                          *
 ******************************************************************************/

TEST_CASE("astlLoadCollection rejects null params", "[wrapper][cache][bad parameters]") {
  REQUIRE(astlLoadCollection(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection rejects wrong _size", "[wrapper][cache][bad parameters]") {
  astl_load_params_t params{};
  params._size             = 1;  // deliberately wrong
  params._input_file_path  = "dummy.astl";
  params._chunk_size_bytes = 0;
  params._flags            = 0;
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
}

TEST_CASE("astlLoadCollection rejects non-zero flags", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_load_params_t, params, ._input_file_path = "dummy.astl", ._chunk_size_bytes = 0, ._flags = 0);
  params._flags = 1;  // reserved, must be 0
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection rejects null input_file_path", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_load_params_t, params, ._input_file_path = nullptr, ._chunk_size_bytes = 0, ._flags = 0);
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection rejects empty input_file_path", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_load_params_t, params, ._input_file_path = "", ._chunk_size_bytes = 0, ._flags = 0);
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection fails for non-existent file", "[wrapper][cache]") {
  // Wrap in an injector so the singleton is restored after the test.
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  ASTL_INIT_STRUCT(astl_load_params_t, params, ._input_file_path = "/tmp/astl_nonexistent_12345.astl",
                   ._chunk_size_bytes = 0, ._flags = 0);
  REQUIRE(astlLoadCollection(&params) != ASTL_STATUS_SUCCESS);
}
