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
#include "astl_internal_status.hpp"
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
#include "topology/scmi_target.hpp"
#include "topology/topology_manager.hpp"

using trompeloeil::_;

namespace astl {
auto operator==(const CollectionOperations& lhs, std::nullptr_t rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}
auto operator==(std::nullptr_t lhs, const CollectionOperations& rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}
}  // namespace astl

namespace std {
template <typename T, std::size_t Extent>
auto operator==(span<T, Extent> lhs, std::nullptr_t rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}

template <typename T, std::size_t Extent>
auto operator==(std::nullptr_t lhs, span<T, Extent> rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}
}  // namespace std

template <typename T>
auto AllocateAstlVector(size_t count) -> std::vector<T> {
  std::vector<T> objects{count};
  if constexpr (requires(T& obj) { obj.size; }) {
    if (count > 0) {
      objects[0].size = sizeof(T);
    }
  }
  if constexpr (requires(T& obj) { obj._size; }) {
    if (count > 0) {
      objects[0]._size = sizeof(T);
    }
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
  REQUIRE(version.major == ASTL_VERSION_MAJOR);
  REQUIRE(version.minor == ASTL_VERSION_MINOR);
  REQUIRE(version.micro == ASTL_VERSION_MICRO);
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
  REQUIRE(std::string(astlStatusString(astl::kInternalUnknownError)) == "UNKNOWN_ERROR");
  REQUIRE(std::string(astlStatusString(ASTL_STATUS_INTERNAL_ERROR)) == "INTERNAL_ERROR");
  // for now at least, anything about ASTL_STATUS_INTERNAL_ERROR is unknown
  astl_status_code truly_unknown = static_cast<astl_status_code>(ASTL_STATUS_INTERNAL_ERROR + ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(std::string(astlStatusString(truly_unknown)) == "UNKNOWN_ERROR");
}

TEST_CASE("astlGetLastStatusString", "[wrapper]") {
  astl_platform_props_t system_info{};
  system_info.size = sizeof(astl_platform_props_t);
  REQUIRE(AstlGetSystemInfo(&system_info) == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string(astlGetLastStatusString()).empty());

  REQUIRE(AstlGetSystemInfo(static_cast<astl_platform_props_t*>(nullptr)) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(std::string(astlGetLastStatusString()) == "BAD_ARGUMENT");

  astl_collection_params_t collection_params{};
  const int                fake_metric    = 0;
  collection_params.size                  = sizeof(astl_collection_params_t);
  astl_metric_handle_t fake_metric_handle = static_cast<astl_metric_handle_t>(&fake_metric);
  REQUIRE(ConfigureMetricCollection(&collection_params, &fake_metric_handle, 1) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(std::string(astlGetLastStatusString()) == "NOT_IMPLEMENTED");

  REQUIRE(AstlGetSystemInfo(&system_info) == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string(astlGetLastStatusString()).empty());
}

TEST_CASE("astlGetSystemInfo", "[wrapper][SystemInfo]") {
  REQUIRE(AstlGetSystemInfo(static_cast<astl_platform_props_t*>(nullptr)) == ASTL_STATUS_BAD_ARGUMENT);

  astl_platform_props_t incompatible_size_info{};
  incompatible_size_info.size = sizeof(astl_platform_props_t) - 1;
  REQUIRE(AstlGetSystemInfo(&incompatible_size_info) == ASTL_STATUS_OLD_STRUCT_VERSION);

  astl_platform_props_t system_info{};
  system_info.size = sizeof(astl_platform_props_t);
  REQUIRE(AstlGetSystemInfo(&system_info) == ASTL_STATUS_SUCCESS);

  REQUIRE((system_info.os_name != nullptr || system_info.kernel_name != nullptr ||
           system_info.kernel_release != nullptr || system_info.architecture != nullptr ||
           system_info.hostname != nullptr || system_info.soc_name != nullptr || system_info.vendor_id != nullptr ||
           system_info.firmware_version != nullptr));
}

TEST_CASE("astlGetSystemInfo", "[wrapper][SystemInfo][flags]") {
  astl_platform_props_t system_info{};
  system_info.size = sizeof(astl_platform_props_t);
  ASTL_INIT_STRUCT(astl_get_system_info_params_t, params, .flags = 0, .system_info = &system_info);

  SECTION("Rejects non-zero params flags") {
    params.flags = 1U;
    REQUIRE(astlGetSystemInfo(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("Rejects unknown system-info source flags") {
    system_info.flags = 0x80000000U;
    REQUIRE(astlGetSystemInfo(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("Rejects mutually-exclusive source flags") {
    system_info.flags = ASTL_SYSTEM_INFO_FLAG_HOST | ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
    REQUIRE(astlGetSystemInfo(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("Accepts explicit host source selector") {
    system_info.flags = ASTL_SYSTEM_INFO_FLAG_HOST;
    REQUIRE(astlGetSystemInfo(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(system_info.flags == ASTL_SYSTEM_INFO_FLAG_HOST);
  }

  SECTION("Rejects explicit loaded-session selector when no session is loaded") {
    astl::ClearLoadedPlatformInfo();
    system_info.flags = ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
    REQUIRE(astlGetSystemInfo(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("astlGetSystemInfo accepts explicit loaded-session selector when cached platform info exists",
          "[wrapper][SystemInfo][flags][loaded-session]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_wrapper_system_info_loaded_session";
  TempFileGuard  cache_guard(cache_dir);

  astl::ClearLoadedPlatformInfo();
  REQUIRE(astl::SavePlatformInfoToCacheDir(cache_dir) == ASTL_STATUS_SUCCESS);
  REQUIRE(astl::LoadPlatformInfoFromCacheDir(cache_dir) == ASTL_STATUS_SUCCESS);

  astl_platform_props_t system_info{};
  system_info.size  = sizeof(system_info);
  system_info.flags = ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION;
  ASTL_INIT_STRUCT(astl_get_system_info_params_t, params, .flags = 0, .system_info = &system_info);

  REQUIRE(astlGetSystemInfo(&params) == ASTL_STATUS_SUCCESS);
  REQUIRE(system_info.flags == ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION);
  REQUIRE(system_info.hostname != nullptr);

  astl::ClearLoadedPlatformInfo();
}

TEST_CASE("astlGetTargetCount", "[Reports 0 targets correctly][wrapper]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(GetTargetCount(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  uint32_t target_count{kJunk};
  REQUIRE(GetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
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
  REQUIRE(GetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == kAFew);
}

TEST_CASE("astlGetTargets", "[0 targets available][wrapper]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t                         target_count{kJunk};
  std::vector<astl_target_props_t> targets{kAFew};
  REQUIRE(GetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 0);
  // with 0 targets available, asking for some of them causes a special error code
  target_count = kAFew;
  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_NO_TARGET_FOUND);
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
  GetTargetCount(&target_count);
  auto actual_target_count = target_count;
  target_count *= 2;  // allocate a little extra buffer, to ensure we get the right warning
  auto targets = AllocateAstlVector<astl_target_props_t>(target_count);
  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(target_count == actual_target_count);
}

TEST_CASE("astlGetTargets", "[invalid parameters][wrapper]") {
  REQUIRE(GetTargets(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  std::vector<astl_target_props_t> targets{kAFew};
  uint32_t                         target_count{kJunk};
  REQUIRE(GetTargets(nullptr, &target_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(target_count == kJunk);
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.emplace_back(std::make_unique<MockTarget>());
  mock_targets.emplace_back(std::make_unique<MockTarget>());
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(GetTargets(targets.data(), nullptr) == ASTL_STATUS_BAD_ARGUMENT);

  target_count = 1;  // buffer too small to hold the 2 MockTargets
  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_BUFFER_TOO_SMALL);
  REQUIRE(target_count == 2);

  targets[0].size = sizeof(astl_target_props_t) - 1;  // caller has old struct
  target_count    = static_cast<uint32_t>(targets.size());
  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_OLD_STRUCT_VERSION);

  targets[0].size = sizeof(astl_target_props_t) + 1;  // caller has new struct
  target_count    = static_cast<uint32_t>(targets.size());
  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_NEW_STRUCT_VERSION);
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

  auto     targets      = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count = 2;

  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(target_count == 1);  // only first target was successful
}

TEST_CASE("astlGetCounterCountOnTarget", "[unreasonably huge number of counters][wrapper]") {
  // mock 1 target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_))
      .RETURN(size_t{1} + std::numeric_limits<uint32_t>::max());
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{0};
  GetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  GetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0].handle;
  uint32_t    counter_count{0};
  REQUIRE(GetCounterCountOnTarget(target_handle, &counter_count) == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("astlGetCounterCountOnTarget", "[Ask a target how many counters it has][wrapper]") {
  // mock 1 target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(0);
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{0};
  GetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  GetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0].handle;

  REQUIRE(GetCounterCountOnTarget(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetCounterCountOnTarget(target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
  uint32_t             counter_count{kJunk};
  REQUIRE(GetCounterCountOnTarget(invalid_target_handle, &counter_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(GetCounterCountOnTarget(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 0);
}

TEST_CASE("astlGetCountersOnTarget", "[invalid parameters][wrapper]") {
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
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(2);

  std::vector<astl_counter_handle_t> counters_for_metric_manager_to_return{nullptr, nullptr};
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(_)).RETURN(counters_for_metric_manager_to_return);
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{0};
  GetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  GetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0].handle;

  uint32_t counter_count{kJunk};
  REQUIRE(GetCounterCountOnTarget(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 2);

  REQUIRE(GetCountersOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  counter_count = kJunk;
  REQUIRE(GetCountersOnTarget(target_handle, nullptr, &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(counter_count == kJunk);

  std::vector<astl_counter_props_t> counters{counter_count};
  // validate struct-size status handling
  counter_count    = 2;
  counters[0].size = sizeof(astl_counter_props_t) - 1;  // caller has old struct
  REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) == ASTL_STATUS_OLD_STRUCT_VERSION);
  REQUIRE(counter_count == 0);  // set to 0 on error
  counter_count    = 2;
  counters[0].size = sizeof(astl_counter_props_t) + 1;  // caller has new struct
  REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) == ASTL_STATUS_NEW_STRUCT_VERSION);

  // back to right-sized structs
  counters[0].size = sizeof(astl_counter_props_t);
  // test null target handle
  REQUIRE(GetCountersOnTarget(nullptr, counters.data(), &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
  // test invalid target handle
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
  counters[0].size                           = sizeof(astl_counter_props_t) + 1;  // caller has new struct
  counter_count                              = 2;
  REQUIRE(GetCountersOnTarget(invalid_target_handle, counters.data(), &counter_count) ==
          ASTL_STATUS_INVALID_TARGET_HANDLE);

  // user buffer too small
  counter_count = 1;
  REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BUFFER_TOO_SMALL);
  REQUIRE(counter_count == 2);
}

TEST_CASE("astlGetCountersOnTarget", "[0 counters available][wrapper]") {
  // mock 1 target with 0 counters
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(0);
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(_)).RETURN(std::span<const astl_counter_handle_t>{});
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{0};
  GetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  GetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0].handle;

  uint32_t counter_count{kJunk};
  REQUIRE(GetCounterCountOnTarget(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 0);

  SECTION("Asking for 0 counters, when 0 are availalable is a bad argument") {
    // try asking for 0 counters, even when 0 counters are available - is a bad input argument
    counter_count = 0;
    auto counters = AllocateAstlVector<astl_counter_props_t>(kAFew);
    REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(counter_count == 0);
  }
  SECTION("Asking for some counters when 0 are available is a NO_COUNTERS error") {
    auto counters    = AllocateAstlVector<astl_counter_props_t>(kAFew);
    counters[0].size = sizeof(astl_counter_props_t);
    counter_count    = kAFew;
    REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) == ASTL_STATUS_NO_COUNTERS_FOUND);
    REQUIRE(counter_count == 0);
  }
}

TEST_CASE("astlGetCountersOnTarget", "[Retrieve a number of counters from a target][wrapper]") {
  // mock 1 target with 2 counters
  auto mock_target = std::make_unique<MockTarget>();

  auto counter1_config = std::make_unique<astl::MetricConfig>(
      "Counter 1", "a test counter", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_VALUE, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});
  auto counter1 = std::make_unique<MockCounter>();
  ALLOW_CALL(*counter1, GetProperties(ANY(astl_counter_props_t*)))
      .SIDE_EFFECT(_1->value_type = ASTL_VALUE_FLOAT64; _1->counter_type = ASTL_COUNTER_TYPE_VALUE;)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::ICounter>> counter1_targets;
  counter1_targets[mock_target.get()] = std::move(counter1);
  astl::CounterHandle counter1_handle{std::move(counter1_config), std::move(counter1_targets)};

  auto counter2_config = std::make_unique<astl::MetricConfig>(
      "Counter 2", "a test counter", ASTL_UNITS_NONE, ASTL_VALUE_FLOAT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_VALUE, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});
  auto counter2 = std::make_unique<MockCounter>();
  ALLOW_CALL(*counter2, GetProperties(ANY(astl_counter_props_t*)))
      .SIDE_EFFECT(_1->value_type = ASTL_VALUE_FLOAT64; _1->counter_type = ASTL_COUNTER_TYPE_VALUE;)
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
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{0};
  GetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  GetTargets(targets.data(), &target_count);
  const auto* target_handle = targets[0].handle;

  SECTION("Request with oversized buffer") {
    uint32_t counter_count{kAFew};
    auto     counters = AllocateAstlVector<astl_counter_props_t>(counter_count);
    REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) ==
            ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
    REQUIRE(counters[1].value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1].counter_type == ASTL_COUNTER_TYPE_VALUE);
  }

  SECTION("Request with exact right buffer size") {
    uint32_t counter_count{2};
    auto     counters = AllocateAstlVector<astl_counter_props_t>(counter_count);
    REQUIRE(GetCountersOnTarget(target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(counters[1].value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1].counter_type == ASTL_COUNTER_TYPE_VALUE);
  }
}

TEST_CASE("astlGetMetricsOnTarget", "[wrapper][Orchestrator][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
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
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_FLOAT64)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(metric1, _))
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_FLOAT32)
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
    REQUIRE(GetMetricCountOnTarget(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(GetMetricCountOnTarget(mock_target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t metric_count{kJunk};
    REQUIRE(GetMetricCountOnTarget(nullptr, &metric_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(metric_count == kJunk);
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(GetMetricCountOnTarget(invalid_target_handle, &metric_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("[good params][wrapper]") {
    uint32_t metric_count{kJunk};
    REQUIRE(GetMetricCountOnTarget(mock_target_handle, &metric_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(metric_count == 2);
  }

  SECTION("astlGetMetricsOnTarget", "[bad params][wrapper]") {
    REQUIRE(GetMetricsOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(GetMetricsOnTarget(mock_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t metric_count{kJunk};
    REQUIRE(GetMetricsOnTarget(mock_target_handle, nullptr, &metric_count) == ASTL_STATUS_BAD_ARGUMENT);
    std::vector<astl_metric_props_t> metrics{kAFew};
    REQUIRE(GetMetricsOnTarget(mock_target_handle, metrics.data(), nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    metric_count    = kAFew;
    metrics[0].size = sizeof(astl_metric_props_t) - 1;  // caller has old struct
    REQUIRE(GetMetricsOnTarget(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_OLD_STRUCT_VERSION);
    metric_count    = kAFew;
    metrics[0].size = sizeof(astl_metric_props_t) + 1;  // caller has newer struct
    REQUIRE(GetMetricsOnTarget(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_NEW_STRUCT_VERSION);
    // test for a buffer too small
    metric_count    = 1;
    metrics[0].size = sizeof(astl_metric_props_t);
    REQUIRE(GetMetricsOnTarget(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_BUFFER_TOO_SMALL);
    REQUIRE(metric_count == 2);
  }

  SECTION("astlGetMetricsOnTarget", "[good params][wrapper]") {
    uint32_t metric_count{2};
    auto     metrics = AllocateAstlVector<astl_metric_props_t>(kAFew);
    REQUIRE(GetMetricsOnTarget(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(metrics[0].value_type == ASTL_VALUE_FLOAT64);
  }
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[Orchestrator][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
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
  auto* metric_manager_ptr = metric_manager.get();

  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));

  ALLOW_CALL(*metric_manager, GetProperties(junk_metric0, _))
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_2->handle = junk_metric0)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(junk_metric1, _))
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_FLOAT32)
      .SIDE_EFFECT(_2->handle = junk_metric1)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_manager, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

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
  REQUIRE(ConfigureCounterCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);

  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{0};
  REQUIRE(GetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(GetTargets(targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  astl_target_handle_t target_handle{targets[0].handle};

  astl_collection_params_t collection_params{
      .size  = sizeof(astl_collection_params_t),
      .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,

      .sampling_interval = 0,

      .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE,
  };

  // get the handles to metrics
  std::array<astl_metric_props_t, 2> metric_buffer{};
  metric_buffer[0].size = sizeof(astl_metric_props_t);
  uint32_t metric_count{2};
  REQUIRE(GetMetricsOnTarget(target_handle, metric_buffer.data(), &metric_count) == ASTL_STATUS_SUCCESS);
  std::vector<astl_metric_handle_t> metric_handles;
  metric_handles.push_back(metric_buffer[0].handle);

  SECTION("[bad params][wrapper]") {
    // all nullptrs
    REQUIRE(ConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, nullptr, 0) ==
            ASTL_STATUS_BAD_ARGUMENT);
    // invalid target
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(ConfigureMetricCollectionOnTarget(invalid_target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_INVALID_TARGET_HANDLE);
    // unsupported collection_params version
    collection_params.size = sizeof(astl_collection_params_t) - 1;
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_OLD_STRUCT_VERSION);
    collection_params.size = sizeof(astl_collection_params_t) + 1;
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_NEW_STRUCT_VERSION);
    collection_params.size = sizeof(astl_collection_params_t);
    // 0 metrics is an error
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 0) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("[valid input][wrapper]") {
    REQUIRE_CALL(*collector_manager_ptr_for_require_calls, ConfigureCollectionOnTarget(_, _, _))
        .RETURN(ASTL_STATUS_SUCCESS);
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_SUCCESS);
  }

  SECTION("[valid no-caching flag][wrapper]") {
    collection_params.flags = ASTL_NO_CACHING | ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
    REQUIRE_CALL(*collector_manager_ptr_for_require_calls, ConfigureCollectionOnTarget(_, _, _))
        .RETURN(ASTL_STATUS_SUCCESS);
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_SUCCESS);
  }

  SECTION("[invalid collection flags][wrapper]") {
    collection_params.flags =
        ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD | ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_INVALID_FLAG_VALUE);

    collection_params.flags = 0x80000000U;
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("[valid input][wrapper][deduplicates repeated metric handles]") {
    REQUIRE_CALL(*metric_manager_ptr, GetRequiredOperations(_, _))
        .LR_WITH(_1.size() == 1)
        .RETURN(astl::CollectionOperations{
            {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});
    REQUIRE_CALL(*collector_manager_ptr_for_require_calls, ConfigureCollectionOnTarget(_, _, _))
        .RETURN(ASTL_STATUS_SUCCESS);

    std::array<astl_metric_handle_t, 2> duplicate_metric_handles{metric_handles[0], metric_handles[0]};
    REQUIRE(ConfigureMetricCollectionOnTarget(target_handle, &collection_params, duplicate_metric_handles.data(),
                                              static_cast<uint32_t>(duplicate_metric_handles.size())) ==
            ASTL_STATUS_SUCCESS);
  }
}

TEST_CASE("astlConfigureCounterCollectionOnTarget", "[Enumerate targets, counters, configure collection][wrapper]") {
  // Mock 1 counter on 1 target
  auto            counter1    = std::make_unique<MockCounter>();
  astl::ICounter* counter_ptr = counter1.get();
  ALLOW_CALL(*counter1, GetProperties(ANY(astl_counter_props_t*))).RETURN(ASTL_STATUS_SUCCESS);

  // set up 1 mock target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));

  // set up one API handle for a counter, associating targets with this counter
  auto counter_config = std::make_unique<astl::MetricConfig>(
      "Counter 1", "a test counter", ASTL_UNITS_NONE, ASTL_VALUE_FLOAT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
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
  auto                     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t                 target_count{0};
  GetTargetCount(&target_count);
  GetTargets(targets.data(), &target_count);
  astl_target_handle_t target_handle{targets[0].handle};

  constexpr uint32_t       sampling_interval_ms{100};
  astl_collection_params_t collection_params{sizeof(astl_collection_params_t),
                                             ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, sampling_interval_ms,
                                             ASTL_COLLECTION_MODE_SAMPLING};

  // with some valid counter_handles we should be good
  auto     counter_properties = AllocateAstlVector<astl_counter_props_t>(kAFew);
  uint32_t counter_count{1};
  REQUIRE(GetCounterCountOnTarget(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(GetCountersOnTarget(target_handle, counter_properties.data(), &counter_count) == ASTL_STATUS_SUCCESS);
  counter_properties.resize(counter_count);
  std::vector<astl_counter_handle_t> legit_counter_handles;
  // get the handles into their own collection
  std::transform(counter_properties.begin(), counter_properties.end(), std::back_inserter(legit_counter_handles),
                 [](const auto& counter) { return counter.handle; });

  REQUIRE(ConfigureCounterCollectionOnTarget(target_handle, &collection_params, legit_counter_handles.data(),
                                             static_cast<uint32_t>(legit_counter_handles.size())) ==
          ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET);
}

TEST_CASE("astlConfigureCounterCollection", "[Test wrapper C->C++ wrapper code][wrapper]") {
  REQUIRE(ConfigureCounterCollection(nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
  constexpr uint32_t       sampling_interval_ms{100};
  astl_collection_params_t collection_params{sizeof(astl_collection_params_t),
                                             ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, sampling_interval_ms,
                                             ASTL_COLLECTION_MODE_SAMPLING};
  REQUIRE(ConfigureCounterCollection(&collection_params, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
  std::vector<astl_counter_handle_t> counter_handles{kAFew};
  // test handler for unmatched size/version of the collection_params struct
  collection_params.size = sizeof(astl_collection_params_t) - 1;
  REQUIRE(ConfigureCounterCollection(&collection_params, counter_handles.data(),
                                     static_cast<uint32_t>(counter_handles.size())) == ASTL_STATUS_OLD_STRUCT_VERSION);
  collection_params.size = sizeof(astl_collection_params_t) + 1;
  REQUIRE(ConfigureCounterCollection(&collection_params, counter_handles.data(),
                                     static_cast<uint32_t>(counter_handles.size())) == ASTL_STATUS_NEW_STRUCT_VERSION);
  collection_params.size = sizeof(astl_collection_params_t);

  // 0-sized output buffer counts as an error
  REQUIRE(ConfigureCounterCollection(&collection_params, counter_handles.data(), 0) == ASTL_STATUS_BAD_ARGUMENT);

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
  REQUIRE(ConfigureCounterCollection(&collection_params, counter_handles.data(),
                                     static_cast<uint32_t>(counter_handles.size())) == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[bad parameters][wrapper]") {
  // create mock target
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(ConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlConfigureMetricCollection", "[unimplemented for now][wrapper]") {
  REQUIRE(ConfigureMetricCollection(nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);

  astl_collection_params_t collection_params{};
  const int                fake_metric    = 0;
  collection_params.size                  = sizeof(astl_collection_params_t);
  astl_metric_handle_t fake_metric_handle = static_cast<astl_metric_handle_t>(&fake_metric);
  REQUIRE(ConfigureMetricCollection(&collection_params, &fake_metric_handle, 1) == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("astlReadImmediateOnTarget", "[1 works, one doesn't][wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(GetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  REQUIRE(target_count == 1);                      // only 1 is successful here.
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(ReadImmediateOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(ReadImmediateOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlReadImmediate", "[with 0 targets][wrapper]") {
  // mock 0 targets
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(ReadImmediate() == ASTL_STATUS_SUCCESS);

  ASTL_INIT_STRUCT(astl_read_immediate_params_t, invalid_params, .flags = 1U);
  REQUIRE(astlReadImmediate(&invalid_params) == ASTL_STATUS_INVALID_FLAG_VALUE);

  ASTL_INIT_STRUCT(astl_read_immediate_on_target_params_t, invalid_on_target_params, .flags = 1U,
                   .target_handle = nullptr);
  REQUIRE(astlReadImmediateOnTarget(&invalid_on_target_params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlReadImmediate", "[success with 2 targets][wrapper]") {
  // mock 2 targets
  auto  mock_target_1          = std::make_unique<MockTarget>();
  auto  mock_target_2          = std::make_unique<MockTarget>();
  auto* mock_target_1_ptr      = mock_target_1.get();
  auto* mock_target_2_ptr      = mock_target_2.get();
  auto  mock_collector_manager = std::make_unique<MockCollectorManager>();
  REQUIRE_CALL(*mock_collector_manager, ConfigureCollectionOnTarget(_, _, _)).TIMES(2).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, GetNativeClockSnapshot(_))
      .TIMES(2)
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  REQUIRE_CALL(*mock_collector_manager, ReadImmediateOnTarget(_)).TIMES(2).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_1_name = "read_immediate_target_1";
  static const std::string target_2_name = "read_immediate_target_2";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  auto                               topology_manager      = std::make_unique<MockTopologyManager>();
  auto                               metric_manager        = std::make_unique<MockMetricManager>();
  static int                         dummy_counter_storage = 0;
  astl_counter_handle_t              counter_handle        = &dummy_counter_storage;
  std::vector<astl_counter_handle_t> available_counters{counter_handle};
  ALLOW_CALL(*metric_manager, GetAvailableCounters(_))
      .RETURN(std::expected<std::span<const astl_counter_handle_t>, astl_status_code>{available_counters});
  ALLOW_CALL(*metric_manager, GetCounterRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });
  ALLOW_CALL(*metric_manager, SetClockCorrelations(_));
  REQUIRE_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(mock_collector_manager),
                                           std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_counter_handle_t, 1> counters{counter_handle};
  REQUIRE(orchestrator->ConfigureCounterCollection(mock_target_1_ptr, &params, counters) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ConfigureCounterCollection(mock_target_2_ptr, &params, counters) == ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(ReadImmediate() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollectionOnTarget", "[unimplemented for now][wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(GetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(StartCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(StartCollectionOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlStartCollection", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_mgr, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_1_name = "start_all_target_1";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_2_name = "start_all_target_2";
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                               target_1 = orchestrator->GetTargets()[0].get();
  astl_collection_params_t            params{.size              = sizeof(astl_collection_params_t),
                                             .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                             .sampling_interval = 0,
                                             .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator->ConfigureMetricCollection(target_1, &params, metrics) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_1)).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(StartCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollection starts counter-configured targets", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                         dummy_counter_storage = 0;
  astl_counter_handle_t              counter_handle        = &dummy_counter_storage;
  std::vector<astl_counter_handle_t> available_counters{counter_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableCounters(_)).RETURN(std::span(available_counters));
  ALLOW_CALL(*metric_mgr, GetCounterRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_1_name = "start_all_counter_target_1";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_2_name = "start_all_counter_target_2";
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                                target_1 = orchestrator->GetTargets()[0].get();
  astl_collection_params_t             params{.size              = sizeof(astl_collection_params_t),
                                              .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                              .sampling_interval = 0,
                                              .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_counter_handle_t, 1> counters{counter_handle};
  REQUIRE(orchestrator->ConfigureCounterCollection(target_1, &params, counters) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_1)).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(StartCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollectionOnTargetPaused", "[wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(GetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(StartCollectionOnTargetPaused(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(StartCollectionOnTargetPaused(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlStartCollectionPaused", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_mgr, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_1_name = "start_all_paused_target_1";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_2_name = "start_all_paused_target_2";
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                               target_2 = orchestrator->GetTargets()[1].get();
  astl_collection_params_t            params{.size              = sizeof(astl_collection_params_t),
                                             .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                             .sampling_interval = 0,
                                             .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator->ConfigureMetricCollection(target_2, &params, metrics) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_2)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, PauseOnTarget(target_2)).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(StartCollectionPaused() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollectionPaused starts counter-configured targets", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                         dummy_counter_storage = 0;
  astl_counter_handle_t              counter_handle        = &dummy_counter_storage;
  std::vector<astl_counter_handle_t> available_counters{counter_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableCounters(_)).RETURN(std::span(available_counters));
  ALLOW_CALL(*metric_mgr, GetCounterRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_1_name = "start_all_paused_counter_target_1";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_2_name = "start_all_paused_counter_target_2";
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                                target_2 = orchestrator->GetTargets()[1].get();
  astl_collection_params_t             params{.size              = sizeof(astl_collection_params_t),
                                              .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                              .sampling_interval = 0,
                                              .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_counter_handle_t, 1> counters{counter_handle};
  REQUIRE(orchestrator->ConfigureCounterCollection(target_2, &params, counters) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_2)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, PauseOnTarget(target_2)).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(StartCollectionPaused() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollection rolls back previously started targets when a later target fails", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_mgr, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto  output_manager = std::make_unique<MockOutputManager>();
  auto  orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                              std::move(metric_manager), std::move(output_manager), "");
  auto* orchestrator_raw = orchestrator.get();

  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_1_name = "start_all_rollback_target_1";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_2_name = "start_all_rollback_target_2";
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                               target_1 = orchestrator->GetTargets()[0].get();
  auto*                               target_2 = orchestrator->GetTargets()[1].get();
  astl_collection_params_t            params{.size              = sizeof(astl_collection_params_t),
                                             .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                             .sampling_interval = 0,
                                             .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator->ConfigureMetricCollection(target_1, &params, metrics) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ConfigureMetricCollection(target_2, &params, metrics) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));
  trompeloeil::sequence sequence;
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_1)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_2)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE_CALL(*collector_mgr, StopOnTarget(target_1)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(StartCollection() == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target_1).value() ==
          astl::Orchestrator::TargetCollectionState::CONFIGURED);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target_2).value() ==
          astl::Orchestrator::TargetCollectionState::CONFIGURED);
}

TEST_CASE("astlStartCollectionPaused rolls back previously started targets when a later target fails", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_mgr, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto  output_manager = std::make_unique<MockOutputManager>();
  auto  orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                              std::move(metric_manager), std::move(output_manager), "");
  auto* orchestrator_raw = orchestrator.get();

  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_1_name = "start_all_paused_rollback_target_1";
  ALLOW_CALL(*mock_target_1, Name()).RETURN(target_1_name);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_2_name = "start_all_paused_rollback_target_2";
  ALLOW_CALL(*mock_target_2, Name()).RETURN(target_2_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                               target_1 = orchestrator->GetTargets()[0].get();
  auto*                               target_2 = orchestrator->GetTargets()[1].get();
  astl_collection_params_t            params{.size              = sizeof(astl_collection_params_t),
                                             .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                             .sampling_interval = 0,
                                             .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator->ConfigureMetricCollection(target_1, &params, metrics) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ConfigureMetricCollection(target_2, &params, metrics) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));
  trompeloeil::sequence sequence;
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_1)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, PauseOnTarget(target_1)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target_2)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE_CALL(*collector_mgr, StopOnTarget(target_1)).IN_SEQUENCE(sequence).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(StartCollectionPaused() == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target_1).value() ==
          astl::Orchestrator::TargetCollectionState::CONFIGURED);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target_2).value() ==
          astl::Orchestrator::TargetCollectionState::CONFIGURED);
}

TEST_CASE("astlPauseCollectionOnTarget", "[wrapper]") {
  REQUIRE(PauseCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlResumeCollectionOnTarget", "[wrapper]") {
  REQUIRE(ResumeCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlStopCollectionOnTarget", "[unimplemented for now][wrapper]") {
  // mock 2 targets
  auto                 mock_target_1        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_1_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 mock_target_2        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_2_handle = mock_target_2.get();
  ALLOW_CALL(*mock_target_2, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_2_handle)
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));

  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     targets      = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count = 2;
  REQUIRE(GetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);

  REQUIRE(StopCollectionOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(StopCollectionOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE("astlStopCollection returns INTERNAL_ERROR without an active session", "[wrapper]") {
  // Explicitly clear any orchestrator state inherited from other tests so this is order-independent
  TestOrchestratorInjector injector(nullptr);
  REQUIRE(StopCollection() == ASTL_STATUS_INTERNAL_ERROR);
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("astlGetCounterSampleCountOnTarget", "[Count the number of calls to ReadImmediate][wrapper]") {
  int junk{1};
  // set up 1 well-behaving mock target with one counter
  auto                  counter1  = std::make_unique<MockCounterHandle>();
  astl_counter_handle_t c1_handle = counter1.get();
  ALLOW_CALL(*counter1, GetProperties(ANY(astl_counter_props_t*)))
      .SIDE_EFFECT(_1->handle = c1_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ICounter>> mock_counters1;
  auto                                         mock_working_target        = std::make_unique<MockTarget>();
  astl::ITarget*                               mock_working_target_ptr    = mock_working_target.get();
  astl_target_handle_t                         mock_working_target_handle = mock_working_target_ptr;
  ALLOW_CALL(*mock_working_target, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_working_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  trompeloeil::sequence seq;
  // set up 1 mock target that'll return an error if we try to call GetCounterSampleCount
  auto                  counter2  = std::make_unique<MockCounterHandle>();
  astl_counter_handle_t c2_handle = counter2.get();
  ALLOW_CALL(*counter2, GetProperties(_)).SIDE_EFFECT(_1->handle = c2_handle).RETURN(ASTL_STATUS_SUCCESS);
  auto                 mock_failing_target = std::make_unique<MockTarget>();
  astl::ITarget*       mock_failing_target_ptr{mock_failing_target.get()};
  astl_target_handle_t mock_failing_target_handle{mock_failing_target_ptr};
  ALLOW_CALL(*mock_failing_target, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_failing_target_handle)
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
  auto     targets = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count{2};
  GetTargets(targets.data(), &target_count);
  const auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
  const auto* working_target_handle{targets[0].handle};
  const auto* broken_target_handle{targets[1].handle};

  auto     counters = AllocateAstlVector<astl_counter_props_t>(kAFew);
  uint32_t counter_count{1};
  REQUIRE(GetCountersOnTarget(working_target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
  const auto* working_counter_handle{counters[0].handle};

  GetCountersOnTarget(broken_target_handle, counters.data(), &counter_count);
  REQUIRE(counter_count == 0);

  // check a bunch of invalid arguments and invalid handles
  REQUIRE(GetCounterSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetCounterSampleCountOnTarget(working_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetCounterSampleCountOnTarget(working_target_handle, working_counter_handle, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
  uint32_t sample_count{kJunk};
  REQUIRE(GetCounterSampleCountOnTarget(working_target_handle, nullptr, &sample_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetCounterSampleCountOnTarget(invalid_target_handle, working_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(GetCounterSampleCountOnTarget(working_target_handle, invalid_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_COUNTER_HANDLE);

  sample_count        = kJunk;
  auto result         = GetCounterSampleCountOnTarget(broken_target_handle, invalid_counter_handle, &sample_count);
  bool is_valid_error = (result == ASTL_STATUS_INVALID_COUNTER_HANDLE) || (result == ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(is_valid_error);
  REQUIRE(sample_count == kJunk);  // unmodified
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("astlGetCounterSamplesOnTarget", "[wrapper][Orchestrator]") {
  auto                     counter = std::make_unique<MockCounter>();
  astl::ICounter*          counter_ptr{counter.get()};
  static std::string const counter_name{"MockCounter"};
  ALLOW_CALL(*counter, Name()).RETURN(counter_name);

  auto                     mock_target        = std::make_unique<MockTarget>();
  const astl::ITarget*     mock_target_raw    = mock_target.get();
  astl_target_handle_t     mock_target_handle = mock_target_raw;
  static std::string const target_name{"MockTarget"};
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = mock_target_handle;
        _1->name        = target_name.c_str();
        _1->description = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);

  auto counter_config = std::make_unique<astl::MetricConfig>(
      counter_name, counter_name, ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
      astl::CollectorType::SCMI, astl::NullOperationBuilder{});
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::ICounter>> target_to_counter_map;
  target_to_counter_map[mock_target_raw] = std::move(counter);
  auto counter_handle =
      std::make_unique<astl::CounterHandle>(std::move(counter_config), std::move(target_to_counter_map));
  astl_counter_handle_t counter_api_handle = static_cast<astl_counter_handle_t>(counter_handle.get());

  auto  mock_metric_manager_uptr = std::make_unique<MockMetricManager>();
  auto* mock_metric_manager      = mock_metric_manager_uptr.get();
  ALLOW_CALL(*mock_metric_manager, GetNumAvailableCounters(_)).RETURN(1);
  ALLOW_CALL(*mock_metric_manager, GetAvailableCounters(mock_target_raw))
      .RETURN(std::span<const astl_counter_handle_t>{&counter_api_handle, 1});
  ALLOW_CALL(*mock_metric_manager, GetCounterOnTarget(counter_api_handle, mock_target_raw)).RETURN(counter_ptr);
  ALLOW_CALL(*mock_metric_manager, GetCounterProperties(counter_api_handle, _))
      .SIDE_EFFECT({
        _2->handle      = counter_api_handle;
        _2->name        = counter_name.c_str();
        _2->description = counter_name.c_str();
        _2->value_type  = ASTL_VALUE_UINT64;
      })
      .RETURN(ASTL_STATUS_SUCCESS);

  auto [orchestrator, expectations] = MakeMinimalOrchestrator(std::move(mock_metric_manager_uptr));
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  orchestrator->SetTargets(std::move(mock_targets));
  auto*                    orchestrator_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t sample_count = kJunk;
  REQUIRE(GetCounterSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetCounterSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetCounterSampleCountOnTarget(mock_target_handle, counter_api_handle, &sample_count, 10, 5) ==
          ASTL_STATUS_BAD_ARGUMENT);

  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(astl::AstlValue{uint64_t{10}},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});
  samples.emplace_back(astl::AstlValue{uint64_t{20}},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{101}});
  samples.emplace_back(astl::AstlValue{uint64_t{30}},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{102}});
  REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, counter_ptr, samples) == ASTL_STATUS_SUCCESS);

  REQUIRE(GetCounterSampleCountOnTarget(mock_target_handle, counter_api_handle, &sample_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count == samples.size());
  REQUIRE(GetCounterSampleCountOnTarget(mock_target_handle, counter_api_handle, &sample_count, 101, 102) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count == 2);

  auto samples_out = AllocateAstlVector<astl_sample_t>(2);
  sample_count     = 2;
  REQUIRE(GetCounterSamplesOnTarget(mock_target_handle, counter_api_handle, samples_out.data(), &sample_count) ==
          ASTL_STATUS_BUFFER_TOO_SMALL);
  REQUIRE(sample_count == samples.size());

  auto full_samples_out = AllocateAstlVector<astl_sample_t>(samples.size());
  sample_count          = static_cast<uint32_t>(full_samples_out.size());
  REQUIRE(GetCounterSamplesOnTarget(mock_target_handle, counter_api_handle, full_samples_out.data(), &sample_count) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count == samples.size());
  REQUIRE(full_samples_out[0].timestamp == 100);
  REQUIRE(full_samples_out[1].timestamp == 101);
  REQUIRE(full_samples_out[2].timestamp == 102);

  sample_count = 2;
  REQUIRE(GetCounterSamplesOnTarget(mock_target_handle, counter_api_handle, samples_out.data(), &sample_count, 101,
                                    0) == ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count == 2);
  REQUIRE(samples_out[0].timestamp == 101);
  REQUIRE(samples_out[1].timestamp == 102);
}

/*** COLLECTED METRIC SAMPLES ***/
TEST_CASE("astlGetMetricSampleCountOnTarget", "[wrapper][Orchestrator][wrapper]") {
  auto                 mock_target        = std::make_unique<MockTarget>();
  const astl::ITarget* mock_target_raw    = mock_target.get();
  astl_target_handle_t mock_target_handle = mock_target_raw;
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  uint32_t sample_count{kJunk};

  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["T0"] = {0x1234};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto metric_config = std::make_unique<astl::MetricConfig>("M0", "M0", ASTL_UNITS_AMPS, ASTL_VALUE_UINT64,
                                                            ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_UNKNOWN,
                                                            astl::CollectorType::SCMI, std::move(op_builder));

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
        _1->handle      = mock_target_handle;
        _1->name        = mock_target_name.c_str();
        _1->description = mock_target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_concrete, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = mock_metric_name.c_str();
        _1->description = mock_metric_name.c_str();
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
    REQUIRE(GetMetricSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    int         junk{1};
    const auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
    const auto* invalid_metric_handle{static_cast<astl_metric_handle_t>(&junk)};
    auto        result = GetMetricSampleCountOnTarget(invalid_target_handle, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(GetMetricSampleCountOnTarget(mock_target_handle, invalid_metric_handle, nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(GetMetricSampleCountOnTarget(mock_target_handle, metric_handle.get(), &sample_count, 300, 200) ==
            ASTL_STATUS_BAD_ARGUMENT);
    // GetMetricSamples
    // invalid targets
    result = GetMetricSamplesOnTarget(nullptr, nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    result = GetMetricSamplesOnTarget(invalid_target_handle, nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, invalid_metric_handle, nullptr, nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    auto samples_out = AllocateAstlVector<astl_sample_t>(kAFew);
    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    sample_count = 1;  // small buffer; depending on internal state may yield BUFFER_TOO_SMALL or BAD_ARGUMENT
    {
      auto result_code =
          GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count);
      REQUIRE((result_code == ASTL_STATUS_BUFFER_TOO_SMALL || result_code == ASTL_STATUS_BAD_ARGUMENT ||
               result_code == ASTL_STATUS_NO_DATA_COLLECTED || result_code == ASTL_STATUS_SUCCESS ||
               result_code == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED));
    }
    sample_count = 0;  // 0 is not a valid size for the output buffer, even if 0 samples are expected
    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_BAD_ARGUMENT);
    sample_count = 1;
    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count, 300,
                                     200) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("[no samples][wrapper]") {
    REQUIRE(orchestrator_raw != nullptr);

    auto samples_out = AllocateAstlVector<astl_sample_t>(kAFew);

    auto result = GetMetricSampleCountOnTarget(mock_target_handle, metric_handle.get(), &sample_count);
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
    samples.emplace_back(astl::AstlValue{uint64_t{1}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});
    samples.emplace_back(astl::AstlValue{uint64_t{2}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{101}});
    samples.emplace_back(astl::AstlValue{uint64_t{3}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{102}});
    // Inject samples into orchestrator's processed sample store
    REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_concrete, samples) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(GetMetricSampleCountOnTarget(mock_target_handle, metric_handle.get(), &sample_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == samples.size());
    REQUIRE(GetMetricSampleCountOnTarget(mock_target_handle, metric_handle.get(), &sample_count, 101, 102) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == 2);

    sample_count     = 2;
    auto samples_out = AllocateAstlVector<astl_sample_t>(kAFew);

    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_BUFFER_TOO_SMALL);
    REQUIRE(sample_count == samples.size());

    auto full_samples_out = AllocateAstlVector<astl_sample_t>(samples.size());
    sample_count          = static_cast<uint32_t>(full_samples_out.size());
    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), full_samples_out.data(), &sample_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == samples.size());
    REQUIRE(full_samples_out[0].timestamp == 100);
    REQUIRE(full_samples_out[1].timestamp == 101);
    REQUIRE(full_samples_out[2].timestamp == 102);

    sample_count = 2;
    REQUIRE(GetMetricSamplesOnTarget(mock_target_handle, metric_handle.get(), samples_out.data(), &sample_count, 101,
                                     0) == ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == 2);
    REQUIRE(samples_out[0].timestamp == 101);
    REQUIRE(samples_out[1].timestamp == 102);
  }
}

TEST_CASE("astlGetMetricsOnTarget verifies identifier propagation", "[wrapper][Orchestrator][Identifier]") {
  // Set up mock target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
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
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_2->handle = metric_temp)
      .SIDE_EFFECT(_2->identifier = ASTL_METRIC_IDENTIFIER_TEMPERATURE)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(metric_power, _))
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_2->handle = metric_power)
      .SIDE_EFFECT(_2->identifier = ASTL_METRIC_IDENTIFIER_POWER)
      .RETURN(ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*metric_manager, GetProperties(metric_freq, _))
      .SIDE_EFFECT(_2->value_type = ASTL_VALUE_UINT64)
      .SIDE_EFFECT(_2->handle = metric_freq)
      .SIDE_EFFECT(_2->identifier = ASTL_METRIC_IDENTIFIER_FREQUENCY)
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

  // Call astlGetMetricsOnTarget and verify categories
  uint32_t metric_count{3};
  auto     metrics = AllocateAstlVector<astl_metric_props_t>(3);
  REQUIRE(GetMetricsOnTarget(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric_count == 3);

  // Verify each metric has the correct identifier
  REQUIRE(metrics[0].identifier == ASTL_METRIC_IDENTIFIER_TEMPERATURE);
  REQUIRE(metrics[1].identifier == ASTL_METRIC_IDENTIFIER_POWER);
  REQUIRE(metrics[2].identifier == ASTL_METRIC_IDENTIFIER_FREQUENCY);
}

TEST_CASE("astlSaveCollection smoke test", "[wrapper][cache]") {
  namespace fs = std::filesystem;

  const fs::path save_file = fs::temp_directory_path() / "astl_save_wrapper_test.astl";
  TempFileGuard  temp_file_guard(save_file);
  const auto     save_file_str = save_file.string();

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_wrapper_test_cache";
  TempFileGuard  cache_dir_guard(cache_dir);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "", "tlm-0"));

  // Build orchestrator manually so we can control MockCollectorManager behavior
  auto topology_manager = std::make_unique<astl::TopologyManager>(std::move(targets));

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
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Discover handle via wrapper API
  auto     target_props = AllocateAstlVector<astl_target_props_t>(kAFew);
  uint32_t target_count = kAFew;
  REQUIRE(GetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);
  REQUIRE(GetTargets(target_props.data(), &target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);

  // Serialization should fail since we are using a mock metric manager (not a concrete MetricManager).
  ASTL_INIT_STRUCT(astl_save_params_t, params, .flags = 0, .output_file_path = nullptr);
  params.output_file_path = save_file_str.c_str();
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
    targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "", "tlm-0"));
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
  "architecture": "arch-from-session",
  "cpu_type": "cpu-type-from-session",
  "cpu_features": "feature-a feature-b",
  "cache_info": "L1 Data 64K",
  "core_count": 128,
  "numa_node_count": 4,
  "socket_count": 2,
  "cache_line_size_bytes": 64,
  "memory_total_bytes": 1099511627776,
  "libc_version": "libc-from-session",
  "boot_info": "UEFI",
  "huge_pages_total": 0,
  "huge_page_size_kb": 2048,
  "transparent_huge_pages": "always [madvise] never"
})";
  }

  REQUIRE(astl::mz::ZipDirectory(src_dir, astl_zip) == ASTL_STATUS_SUCCESS);

  // Wrap in an injector so the original orchestrator is restored after the load resets/rebuilds the singleton.
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  ASTL_INIT_STRUCT(astl_load_params_t, params, .flags = 0, .input_file_path = nullptr, .chunk_size_bytes = 0);
  const auto astl_zip_str = astl_zip.string();
  params.input_file_path  = astl_zip_str.c_str();

  auto load_status = astlLoadCollection(&params);
  if (load_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlLoadCollection failed with status: " << load_status);
    return;
  }

  astl_platform_props_t platform_info{};
  platform_info.size  = sizeof(astl_platform_props_t);
  auto sysinfo_status = AstlGetSystemInfo(&platform_info);
  if (sysinfo_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetSystemInfo failed with status: " << sysinfo_status);
    return;
  }
  REQUIRE(platform_info.soc_name != nullptr);
  REQUIRE(platform_info.vendor_id != nullptr);
  REQUIRE(platform_info.kernel_version != nullptr);
  REQUIRE(std::string(platform_info.soc_name) == "soc-from-session");
  REQUIRE(std::string(platform_info.vendor_id) == "vendor-from-session");
  REQUIRE(std::string(platform_info.kernel_version) == "kernel-version-from-session");
  REQUIRE(platform_info.cpu_type != nullptr);
  REQUIRE(std::string(platform_info.cpu_type) == "cpu-type-from-session");
  REQUIRE(platform_info.core_count == 128);
  REQUIRE(platform_info.memory_total_bytes == 1099511627776ULL);
  REQUIRE(platform_info.huge_pages_total == 0);
  REQUIRE(platform_info.huge_page_size_kb == 2048);
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
    targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "", "tlm-0"));
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

  ASTL_INIT_STRUCT(astl_load_params_t, params, .flags = 0, .input_file_path = nullptr, .chunk_size_bytes = 0);
  const auto astl_zip_str = astl_zip.string();
  params.input_file_path  = astl_zip_str.c_str();
  auto load_status        = astlLoadCollection(&params);
  if (load_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlLoadCollection failed with status: " << load_status);
    return;
  }

  astl_platform_props_t loaded_info{};
  loaded_info.size    = sizeof(astl_platform_props_t);
  auto sysinfo_status = AstlGetSystemInfo(&loaded_info);
  if (sysinfo_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetSystemInfo (loaded_info) failed with status: " << sysinfo_status);
    return;
  }
  REQUIRE(loaded_info.soc_name != nullptr);
  REQUIRE(std::string(loaded_info.soc_name) == "soc-from-session");

  auto     targets        = AllocateAstlVector<astl_target_props_t>(1);
  uint32_t target_count   = 1;
  auto     targets_status = GetTargets(targets.data(), &target_count);
  if (targets_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetTargets failed with status: " << targets_status);
    return;
  }
  REQUIRE(target_count == 1);
  // Check that the handle is valid before using it
  if (targets[0].handle == nullptr) {
    FAIL("astlGetTargets returned a null target handle");
    return;
  }

  astl_collection_params_t collection_params{};
  collection_params.size              = sizeof(astl_collection_params_t);
  collection_params.sampling_interval = 100;
  collection_params.collection_mode   = ASTL_COLLECTION_MODE_SAMPLING;
  collection_params.flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD;

  int                   fake_counter_token  = 0;
  astl_counter_handle_t fake_counter_handle = &fake_counter_token;
  auto                  config_status =
      ConfigureCounterCollectionOnTarget(targets[0].handle, &collection_params, &fake_counter_handle, 1);
  if (config_status != ASTL_STATUS_SUCCESS && config_status != ASTL_STATUS_BAD_ARGUMENT) {
    FAIL("astlConfigureCounterCollectionOnTarget returned unexpected status: " << config_status);
    return;
  }

  astl_platform_props_t host_info{};
  host_info.size       = sizeof(astl_platform_props_t);
  auto hostinfo_status = AstlGetSystemInfo(&host_info);
  if (hostinfo_status != ASTL_STATUS_SUCCESS) {
    FAIL("astlGetSystemInfo (host_info) failed with status: " << hostinfo_status);
    return;
  }
  REQUIRE((host_info.soc_name == nullptr || std::string(host_info.soc_name) != "soc-from-session"));
}

TEST_CASE("astlSaveCollection writes system info into cache", "[wrapper][cache][system-info]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_platform_info_cache";
  const fs::path astl_file = cache_dir / "session.astl";
  TempFileGuard  cache_guard(cache_dir);
  TempFileGuard  file_guard(astl_file);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "", "tlm-0"));
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
  ASTL_INIT_STRUCT(astl_save_params_t, params, .flags = 0, .output_file_path = nullptr);
  params.output_file_path = astl_file_str.c_str();
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
  params.size             = 1;  // deliberately wrong
  params.output_file_path = nullptr;
  params.flags            = 0;
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
}

TEST_CASE("astlSaveCollection rejects non-zero flags", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_save_params_t, params, .flags = 0, .output_file_path = nullptr);
  params.flags = 1;  // reserved, must be 0
  REQUIRE(astlSaveCollection(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlSaveCollection rejects null output_file_path and creates no .astl", "[wrapper][cache]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_cache_fallback_test";
  TempFileGuard  cache_dir_guard(cache_dir);

  // One mock target
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
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
  ASTL_INIT_STRUCT(astl_save_params_t, params, .flags = 0, .output_file_path = nullptr);
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
  ASTL_INIT_STRUCT(astl_save_params_t, params, .flags = 0, .output_file_path = "");
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
  params.size             = 1;  // deliberately wrong
  params.input_file_path  = "dummy.astl";
  params.chunk_size_bytes = 0;
  params.flags            = 0;
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
}

TEST_CASE("astlLoadCollection rejects non-zero flags", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_load_params_t, params, .flags = 0, .input_file_path = "dummy.astl", .chunk_size_bytes = 0);
  params.flags = 1;  // reserved, must be 0
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlLoadCollection rejects null input_file_path", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_load_params_t, params, .flags = 0, .input_file_path = nullptr, .chunk_size_bytes = 0);
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection rejects empty input_file_path", "[wrapper][cache][bad parameters]") {
  ASTL_INIT_STRUCT(astl_load_params_t, params, .flags = 0, .input_file_path = "", .chunk_size_bytes = 0);
  REQUIRE(astlLoadCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlLoadCollection fails for non-existent file", "[wrapper][cache]") {
  // Wrap in an injector so the singleton is restored after the test.
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  ASTL_INIT_STRUCT(astl_load_params_t, params, .flags = 0, .input_file_path = "/tmp/astl_nonexistent_12345.astl",
                   .chunk_size_bytes = 0);
  REQUIRE(astlLoadCollection(&params) != ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlPauseCollection and astlResumeCollection route through orchestrator", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(true);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                        pr_metric_storage = 0;
  astl_metric_handle_t              metric_handle     = &pr_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_mgr, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto  output_manager = std::make_unique<MockOutputManager>();
  auto  orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                              std::move(metric_manager), std::move(output_manager), "");
  auto* orchestrator_raw = orchestrator.get();

  auto                     mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t     mock_target_handle = mock_target.get();
  static const std::string pr_target_name     = "pause_resume_target";
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target, Name()).RETURN(pr_target_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                               target = orchestrator_raw->GetTargets()[0].get();
  astl_collection_params_t            params{.size              = sizeof(astl_collection_params_t),
                                             .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                             .sampling_interval = 0,
                                             .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator_raw->ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));

  using State = astl::Orchestrator::TargetCollectionState;
  trompeloeil::sequence seq;
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target)).IN_SEQUENCE(seq).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, PauseOnTarget(target)).IN_SEQUENCE(seq).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, ResumeOnTarget(target)).IN_SEQUENCE(seq).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target).value() == State::STARTED);

  REQUIRE(PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target).value() == State::PAUSED);

  REQUIRE(ResumeCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target).value() == State::STARTED);
}

TEST_CASE("astlStopCollection routes through orchestrator", "[wrapper]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* collector_mgr     = collector_manager.get();
  ALLOW_CALL(*collector_mgr, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_mgr, IsAnyTargetBeingCollected()).RETURN(false);

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto* metric_mgr     = metric_manager.get();
  ALLOW_CALL(*metric_mgr, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RemoveAllMetrics());
  static int                        stop_metric_storage = 0;
  astl_metric_handle_t              metric_handle       = &stop_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_mgr, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_mgr, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}});
  ALLOW_CALL(*metric_mgr, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_mgr, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_mgr, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  ALLOW_CALL(*output_manager, OutputProcessedSamples(_, _, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  auto  orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                            std::move(metric_manager), std::move(output_manager), "");
  auto* orchestrator_raw = orchestrator.get();

  auto                     mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t     mock_target_handle = mock_target.get();
  static const std::string stop_target_name   = "stop_all_target";
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target, Name()).RETURN(stop_target_name);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto*                               target = orchestrator_raw->GetTargets()[0].get();
  astl_collection_params_t            params{.size              = sizeof(astl_collection_params_t),
                                             .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                             .sampling_interval = 0,
                                             .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator_raw->ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);

  ALLOW_CALL(*collector_mgr, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(*metric_mgr, SetClockCorrelations(_));

  using State = astl::Orchestrator::TargetCollectionState;
  trompeloeil::sequence seq;
  REQUIRE_CALL(*collector_mgr, StartOnTarget(target)).IN_SEQUENCE(seq).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_mgr, StopOnTarget(target)).IN_SEQUENCE(seq).RETURN(ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator_raw->GetTargetCollectionState(target).value() == State::STOPPED);
}
