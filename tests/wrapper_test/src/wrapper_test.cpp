#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/trompeloeil.hpp>
#include <cstdint>
#include <utility>

#include "../../mock_classes.hpp"
#include "astl/astl.h"
#include "astl/astl_test_hooks.h"
#include "astl_impl.hpp"
#include "counter.hpp"
#include "target.hpp"

using trompeloeil::_;

/**
 * @brief A test harness construct to replace the ASTL's Orchestrator instance with one for testing
 *
 * This is an RAII-style manager; on construction it will swap out the existing orchestrator
 * with the given `test_orchestrator`. On destruction (usually at the end of a test), it'll swap
 * the original orchestrator back in.
 */
class TestOrchestratorInjector {
 public:
  TestOrchestratorInjector() = delete;  // we must provide an orchestrator to inject

  /**
   * @brief Replace the existing `orchestrator` with the given test_orchestrator.
   *
   * When this TestOrchestratorInjector is destroyed, it'll put the original orchestrator back
   */
  explicit TestOrchestratorInjector(std::unique_ptr<astl::Orchestrator> test_orchestrator)
      : _test_orchestrator(std::move(test_orchestrator)) {
    astlInjectTestOrchestrator(_test_orchestrator.release(), &_original_orchestrator);
  }

  /**
   * @brief swap the original orchestrator back into the library to resume use as normal
   */
  ~TestOrchestratorInjector() {
    // swap back the original orchestrator, and retrieve the test orchestrator for clean up.
    astl_test_orchestrator_t test_orchestrator_handle{nullptr};
    astlInjectTestOrchestrator(_original_orchestrator, &test_orchestrator_handle);
    // cppcheck-suppress constVariablePointer
    auto* raw_test_orchestrator = static_cast<astl::Orchestrator*>(test_orchestrator_handle);
    _test_orchestrator.reset(raw_test_orchestrator);
  }

  // since we're managing a resource (an original orchestrator and test orchestrator),
  // disallow coppying and moving
  TestOrchestratorInjector(TestOrchestratorInjector const&)            = delete;
  TestOrchestratorInjector(TestOrchestratorInjector&&)                 = delete;
  TestOrchestratorInjector& operator=(TestOrchestratorInjector const&) = delete;
  TestOrchestratorInjector& operator=(TestOrchestratorInjector&&)      = delete;

 private:
  std::unique_ptr<astl::Orchestrator> _test_orchestrator;
  // hold the original orchestrator as a raw handle, since that's how the C interface provides it
  astl_test_orchestrator_t _original_orchestrator{nullptr};
};

// just a couple handy imprecise constants for testing
constexpr uint32_t kJunk = 13;
constexpr uint32_t kAFew = 7;

TEST_CASE("astlVersion", "[matches header definition]") {
  astl_version_t version = astlVersion();
  REQUIRE(version._major == ASTL_VERSION_MAJOR);
  REQUIRE(version._minor == ASTL_VERSION_MINOR);
  REQUIRE(version._micro == ASTL_VERSION_MICRO);
}

TEST_CASE("astlVersionString", "[matches header definition]") {
  const char* version_string = astlVersionString();
  REQUIRE(std::string(version_string) == std::string(ASTL_VERSION_STRING));
}

TEST_CASE("astlStatusString", "[matches header definition]") {
  astl_status_code error        = ASTL_STATUS_BAD_ARGUMENT;
  const char*      error_string = astlStatusString(error);
  REQUIRE(std::string(error_string) == "BAD_ARGUMENT");

  REQUIRE(std::string(astlStatusString(ASTL_STATUS_NO_DATA_COLLECTED)) == "NO_DATA_COLLECTED");
  REQUIRE(std::string(astlStatusString(ASTL_STATUS_INTERNAL_ERROR)) == "INTERNAL_ERROR");
  // for now at least, anything about ASTL_STATUS_INTERNAL_ERROR is unknown
  astl_status_code truly_unknown = static_cast<astl_status_code>(ASTL_STATUS_INTERNAL_ERROR + ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(std::string(astlStatusString(truly_unknown)) == "UNKNOWN_ERROR");
  REQUIRE(std::string(astlStatusString(ASTL_STATUS_INTERNAL_ERROR)) == "INTERNAL_ERROR");
}

TEST_CASE("astlInitialize checks for invalid input", "[wrapper_test]") {
  REQUIRE(astlInitialize(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetTargetCount", "[Reports 0 targets correctly]") {
  auto                     zero_target_orchestrator = std::make_unique<astl::Orchestrator>();
  TestOrchestratorInjector injector(std::move(zero_target_orchestrator));
  REQUIRE(astlGetTargetCount(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  uint32_t target_count{kJunk};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 0);
}

TEST_CASE("astlGetTargetCount", "[Reports a few targets correctly]") {
  // now give it a few targets targets to talk to
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  for (uint32_t i = 0; i < kAFew; ++i) {
    targets.push_back(std::make_unique<MockTarget>());
  }
  auto a_few_targets_orchestrator = std::make_unique<astl::Orchestrator>();
  a_few_targets_orchestrator->SetTargets(std::move(targets));
  TestOrchestratorInjector injector(std::move(a_few_targets_orchestrator));

  uint32_t target_count{kJunk};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == kAFew);
}

TEST_CASE("astlGetTargets", "[0 targets available]") {
  auto                     orchestrator = std::make_unique<astl::Orchestrator>();
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

TEST_CASE("astlGetTargets", "[Oversized buffer]") {
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target_1));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  auto actual_target_count = target_count;
  target_count *= 2;  // allocate a little extra buffer, to ensure we get the right warning
  std::vector<astl_target_properties_t> targets{target_count};
  targets[0]._size = sizeof(astl_target_properties_t);
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(target_count == actual_target_count);
}

TEST_CASE("astlGetTargets", "[invalid parameters]") {
  REQUIRE(astlGetTargets(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  std::vector<astl_target_properties_t> targets{kAFew};
  uint32_t                              target_count{kJunk};
  REQUIRE(astlGetTargets(nullptr, &target_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(target_count == kJunk);
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.emplace_back(std::make_unique<MockTarget>());
  mock_targets.emplace_back(std::make_unique<MockTarget>());
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlGetTargets(targets.data(), nullptr) == ASTL_STATUS_BAD_ARGUMENT);

  target_count = 1;  // buffer too small to hold the 2 MockTargets
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL);

  targets[0]._size = sizeof(astl_target_properties_t) - 1;  // caller has old struct
  target_count     = targets.size();
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION);

  targets[0]._size = sizeof(astl_target_properties_t) + 1;  // caller has new struct
  target_count     = targets.size();
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION);
}

TEST_CASE("astlGetTargets", "[second target can't retrieve parameters]") {
  // mock 2 targets
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target_1));

  auto mock_target_2 = std::make_unique<MockTarget>();
  REQUIRE_CALL(*mock_target_2, GetProperties(_)).RETURN(ASTL_STATUS_INTERNAL_ERROR);
  mock_targets.push_back(std::move(mock_target_2));

  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size      = sizeof(astl_target_properties_t);
  uint32_t target_count = 2;

  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(target_count == 1);  // only first target was successful
}

TEST_CASE("astlGetCounterCount", "[unreasonably huge number of counters]") {
  // mock 1 target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_target_1, GetCounterCount()).RETURN(size_t{1} + std::numeric_limits<uint32_t>::max());
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  auto*    target_handle = targets[0]._handle;
  uint32_t counter_count{0};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL);
}

TEST_CASE("astlGetCounterCount", "[Ask a target how many counters it has]") {
  // mock 1 target
  auto                 mock_target_1      = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_target_1, GetCounterCount()).RETURN(0);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  auto* target_handle = targets[0]._handle;

  REQUIRE(astlGetCounterCount(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterCount(target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
  uint32_t             counter_count{kJunk};
  REQUIRE(astlGetCounterCount(invalid_target_handle, &counter_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 0);

  // TODO(https://github.com/Arm-Debug/ASTL/issues/27) for more counters
}

TEST_CASE("astlGetCounters", "[invalid parameters]") {
  // mock 2 counters
  auto                                         counter1 = std::make_unique<MockCounter>();
  auto                                         counter2 = std::make_unique<MockCounter>();
  std::vector<std::unique_ptr<astl::ICounter>> mock_counters;
  mock_counters.push_back(std::move(counter1));
  mock_counters.push_back(std::move(counter2));
  // mock 1 target with 2 counters
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target = std::make_unique<MockTarget>(std::move(mock_counters));
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, GetCounterCount()).RETURN(2);

  mock_targets.push_back(std::move(mock_target));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  auto* target_handle = targets[0]._handle;

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

TEST_CASE("astlGetCounters", "[0 counters available]") {
  // mock 1 target with 0 counters
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_target, GetCounterCount()).RETURN(0);
  mock_targets.push_back(std::move(mock_target));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  auto* target_handle = targets[0]._handle;

  uint32_t counter_count{kJunk};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(counter_count == 0);

  SECTION("Asking for 0 counters, when 0 are availalable is a bad argument") {
    // try asking for 0 counters, even when 0 counters are available - is a bad input argument
    counter_count = 0;
    std::vector<astl_counter_properties_t> counters{kAFew};
    counters[0]._size = sizeof(astl_counter_properties_t);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(counter_count == 0);
  }
  SECTION("Asking for some counters when 0 are available is a NO_COUNTERS error") {
    std::vector<astl_counter_properties_t> counters{kAFew};
    counter_count     = counters.size();
    counters[0]._size = sizeof(astl_counter_properties_t);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_NO_COUNTERS_FOUND);
    REQUIRE(counter_count == 0);
  }
}

TEST_CASE("astlGetCounters", "[Retrieve a number of counters from a target]") {
  auto counter1 = std::make_unique<MockCounter>();
  auto counter2 = std::make_unique<MockCounter>();
  // set up expectations - we should call GetProperties on each of the counters
  REQUIRE_CALL(*counter1, GetProperties(_)).SIDE_EFFECT(_1->_mask = 0).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*counter2, GetProperties(_))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_1->_counter_type = ASTL_COUNTER_TYPE_VALUE)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::vector<std::unique_ptr<astl::ICounter>> mock_counters;
  mock_counters.push_back(std::move(counter1));
  mock_counters.push_back(std::move(counter2));
  // mock 1 target with 2 counters
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target = std::make_unique<MockTarget>(std::move(mock_counters));
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  auto* target_handle = targets[0]._handle;

  SECTION("Request with oversized buffer") {
    uint32_t                               counter_count{kAFew};
    std::vector<astl_counter_properties_t> counters{counter_count};
    counters[0]._size = sizeof(astl_counter_properties_t);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
    REQUIRE(counters[0]._mask == 0);
    REQUIRE(counters[1]._value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1]._counter_type == ASTL_COUNTER_TYPE_VALUE);
  }

  SECTION("Request with exact right buffer size") {
    uint32_t                               counter_count{2};
    std::vector<astl_counter_properties_t> counters{counter_count};
    counters[0]._size = sizeof(astl_counter_properties_t);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(counters[0]._mask == 0);
    REQUIRE(counters[1]._value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1]._counter_type == ASTL_COUNTER_TYPE_VALUE);
  }
}

TEST_CASE("astlGetMetricCount", "[unimplemented for now]") {
  REQUIRE(astlGetMetricCount(nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetrics", "[unimplemented for now]") {
  REQUIRE(astlGetMetrics(nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroupCount", "[unimplemented for now]") {
  REQUIRE(astlGetMetricGroupCount(nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroups", "[unimplemented for now]") {
  REQUIRE(astlGetMetricGroups(nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroupMetrics", "[unimplemented for now]") {
  REQUIRE(astlGetMetricGroupMetrics(nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureCounterCollectionOnTarget", "[bad params]") {
  // set up a mock target with one mock counter
  auto                                         counter1 = std::make_unique<MockCounter>();
  std::vector<std::unique_ptr<astl::ICounter>> counters;
  counters.push_back(std::move(counter1));
  auto                 mock_target_1      = std::make_unique<MockTarget>(std::move(counters));
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCounterCount()).RETURN(1);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // all nullptrs
  REQUIRE(astlConfigureCounterCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);

  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(astlGetTargets(targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  astl_target_handle_t target_handle{targets[0]._handle};

  // given a realistic target handle, but invalid other params
  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);

  constexpr uint32_t           sampling_interval_ms{100};
  astl_collection_parameters_t collection_params{sizeof(astl_collection_parameters_t), sampling_interval_ms,
                                                 ASTL_COLLECTION_MODE_SAMPLING, ASTL_COLLECTION_OPTIMIZATION_MEMORY};
  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, nullptr, 0) ==
          ASTL_STATUS_BAD_ARGUMENT);
  // Note: normally we'd get these handles from astlGetCounters
  std::vector<astl_counter_handle_t> counter_handles{1};
  // test handler for unmatched size/version of the collection_params struct
  collection_params._size = sizeof(astl_collection_parameters_t) - 1;
  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, counter_handles.data(),
                                                 counter_handles.size()) ==
          ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION);
  collection_params._size = sizeof(astl_collection_parameters_t) + 1;
  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, counter_handles.data(),
                                                 counter_handles.size()) ==
          ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION);
  collection_params._size = sizeof(astl_collection_parameters_t);

  // configuring 0 counters is a bad argument
  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, counter_handles.data(), 0) ==
          ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlConfigureCounterCollectionOnTarget", "[Enumerate targets, counters, configure collection]") {
  // mock 2 targets
  // 1st has one counter
  auto counter1 = std::make_unique<MockCounter>();
  // get this now, as the SIDE_EFFECT action below runs after this ptr has been moved.
  // when the GetProperties is called, we have to at least assign the _handle field of the struct to match the
  // ICounter ptr
  astl_counter_handle_t c1_handle = counter1.get();
  ALLOW_CALL(*counter1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = c1_handle)
      .SIDE_EFFECT(_1->_mask = 0xaced)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*counter1, ConfigureCollection(_)).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ICounter>> counters;
  counters.push_back(std::move(counter1));
  auto                 mock_target_1      = std::make_unique<MockTarget>(std::move(counters));
  astl_target_handle_t mock_target_handle = mock_target_1.get();
  ALLOW_CALL(*mock_target_1, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetCounterCount()).RETURN(1);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  astlGetTargets(targets.data(), &target_count);
  astl_target_handle_t target_handle{targets[0]._handle};

  constexpr uint32_t           sampling_interval_ms{100};
  astl_collection_parameters_t collection_params{sizeof(astl_collection_parameters_t), sampling_interval_ms,
                                                 ASTL_COLLECTION_MODE_SAMPLING, ASTL_COLLECTION_OPTIMIZATION_MEMORY};

  // with some valid counter_handles we should be good
  std::vector<astl_counter_properties_t> counter_properties{kAFew};
  counter_properties[0]._size = sizeof(astl_counter_properties_t);
  uint32_t counter_count{1};
  REQUIRE(astlGetCounterCount(target_handle, &counter_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(astlGetCounters(target_handle, counter_properties.data(), &counter_count) == ASTL_STATUS_SUCCESS);
  counter_properties.resize(counter_count);
  std::vector<astl_counter_handle_t> legit_counter_handles;
  // get the handles into their own collection
  std::transform(counter_properties.begin(), counter_properties.end(), std::back_inserter(legit_counter_handles),
                 [](const auto& counter) { return counter._handle; });

  REQUIRE(astlConfigureCounterCollectionOnTarget(target_handle, &collection_params, legit_counter_handles.data(),
                                                 legit_counter_handles.size()) ==
          ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET);
}

TEST_CASE("astlConfigureCounterCollection", "[Test wrapper C->C++ wrapper code]") {
  REQUIRE(astlConfigureCounterCollection(nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
  constexpr uint32_t           sampling_interval_ms{100};
  astl_collection_parameters_t collection_params{sizeof(astl_collection_parameters_t), sampling_interval_ms,
                                                 ASTL_COLLECTION_MODE_SAMPLING, ASTL_COLLECTION_OPTIMIZATION_MEMORY};
  REQUIRE(astlConfigureCounterCollection(&collection_params, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
  std::vector<astl_counter_handle_t> counter_handles{kAFew};
  // test handler for unmatched size/version of the collection_params struct
  collection_params._size = sizeof(astl_collection_parameters_t) - 1;
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(), counter_handles.size()) ==
          ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION);
  collection_params._size = sizeof(astl_collection_parameters_t) + 1;
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(), counter_handles.size()) ==
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
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Not implemented yet
  REQUIRE(astlConfigureCounterCollection(&collection_params, counter_handles.data(), counter_handles.size()) ==
          ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricCollection", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricCollection(nullptr, nullptr, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

/*** CONFIGURE METRIC GROUPS ***/
TEST_CASE("astlConfigureMetricGroupCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricGroupCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlConfigureMetricGroupCollection", "[unimplemented for now]") {
  REQUIRE(astlConfigureMetricGroupCollection(nullptr, nullptr, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlReadImmediateOnTarget", "[1 works, one doesn't]") {
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

  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size      = sizeof(astl_target_properties_t);
  uint32_t target_count = 2;
  REQUIRE(astlGetTargets(targets.data(), &target_count) ==
          ASTL_STATUS_INTERNAL_ERROR);             // get target properties from our mock target fails
  int                  junk                  = 1;  // not null, but not a valid handle to a target
  astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
  astl_target_handle_t working_target_handle = targets[0]._handle;
  astl_target_handle_t broken_target_handle  = targets[1]._handle;

  REQUIRE(astlReadImmediateOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlReadImmediateOnTarget(invalid_target_handle) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  REQUIRE(astlReadImmediateOnTarget(broken_target_handle) == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("astlReadImmediate", "[with 0 targets]") {
  // mock 0 targets
  auto                     orchestrator = std::make_unique<astl::Orchestrator>();
  TestOrchestratorInjector injector(std::move(orchestrator));
  REQUIRE(astlReadImmediate() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlReadImmediate", "[success with 2 targets]") {
  // mock 2 targets
  auto mock_target_1          = std::make_unique<MockTarget>();
  auto mock_target_2          = std::make_unique<MockTarget>();
  auto mock_collector_manager = std::make_unique<MockCollectorManager>();
  REQUIRE_CALL(*mock_collector_manager, ReadImmediateOnTarget(_)).TIMES(2).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_1, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target_2, GetProperties(_)).RETURN(ASTL_STATUS_SUCCESS);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  mock_targets.push_back(std::move(mock_target_2));
  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(mock_collector_manager), nullptr);
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlReadImmediate() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlStartCollectionOnTarget(nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlStartCollection", "[unimplemented for now]") {
  REQUIRE(astlStartCollection() == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlPauseCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlPauseCollectionOnTarget(nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlPauseCollection", "[unimplemented for now]") {
  REQUIRE(astlPauseCollection() == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlResumeCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlResumeCollectionOnTarget(nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlResumeCollection", "[unimplemented for now]") {
  REQUIRE(astlResumeCollection() == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlStopCollectionOnTarget", "[unimplemented for now]") {
  REQUIRE(astlStopCollectionOnTarget(nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlStopCollection", "[unimplemented for now]") {
  REQUIRE(astlStopCollection() == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetCounterSampleCountOnTarget", "[Count the number of calls to ReadImmediate]") {
  // set up 1 well-behaving mock target with one counter
  auto                  counter1  = std::make_unique<MockCounter>();
  astl_counter_handle_t c1_handle = counter1.get();
  ALLOW_CALL(*counter1, GetProperties(_)).SIDE_EFFECT(_1->_handle = c1_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ICounter>> mock_counters1;
  mock_counters1.push_back(std::move(counter1));
  auto                 mock_working_target        = std::make_unique<MockTarget>(std::move(mock_counters1));
  astl_target_handle_t mock_working_target_handle = mock_working_target.get();
  ALLOW_CALL(*mock_working_target, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_working_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  trompeloeil::sequence seq;
  // set up 1 mock target that'll return an error if we try to call GetCounterSampleCount
  auto                  counter2  = std::make_unique<MockCounter>();
  astl_counter_handle_t c2_handle = counter2.get();
  ALLOW_CALL(*counter2, GetProperties(_)).SIDE_EFFECT(_1->_handle = c2_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ICounter>> mock_counters2;
  mock_counters2.push_back(std::move(counter2));
  auto                 mock_failing_target        = std::make_unique<MockTarget>(std::move(mock_counters2));
  astl_target_handle_t mock_failing_target_handle = mock_failing_target.get();
  ALLOW_CALL(*mock_failing_target, GetProperties(_))
      .SIDE_EFFECT(_1->_handle = mock_failing_target_handle)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_working_target));
  mock_targets.push_back(std::move(mock_failing_target));
  auto orchestrator = std::make_unique<astl::Orchestrator>();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // now that the test objects are in place, use the API as normal to get the handles to our objects
  std::vector<astl_target_properties_t> targets{kAFew};
  targets[0]._size = sizeof(astl_target_properties_t);
  uint32_t target_count{2};
  astlGetTargets(targets.data(), &target_count);
  int   junk{1};
  auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
  auto* working_target_handle{targets[0]._handle};
  auto* broken_target_handle{targets[1]._handle};

  std::vector<astl_counter_properties_t> counters{kAFew};
  counters[0]._size = sizeof(astl_counter_properties_t);
  uint32_t counter_count{1};
  REQUIRE(astlGetCounters(working_target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
  auto* working_counter_handle{counters[0]._handle};

  astlGetCounters(broken_target_handle, counters.data(), &counter_count);
  auto* broken_counter_handle{counters[0]._handle};
  REQUIRE(working_counter_handle != broken_counter_handle);

  // check a bunch of invalid arguments and invalid handles
  REQUIRE(astlGetCounterSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, working_counter_handle, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
  uint32_t sample_count{kJunk};
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, nullptr, &sample_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(astlGetCounterSampleCountOnTarget(invalid_target_handle, working_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_TARGET_HANDLE);
  auto* invalid_counter_handle = static_cast<astl_counter_handle_t>(&junk);
  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, invalid_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_COUNTER_HANDLE);

  sample_count = kJunk;
  REQUIRE(astlGetCounterSampleCountOnTarget(broken_target_handle, broken_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_COUNTER_HANDLE);
  REQUIRE(sample_count == kJunk);  // unmodified

  REQUIRE(astlGetCounterSampleCountOnTarget(working_target_handle, working_counter_handle, &sample_count) ==
          ASTL_STATUS_INVALID_COUNTER_HANDLE);
}

TEST_CASE("astlGetCounterSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetCounterSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSampleCountOnTarget(nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSamplesOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSampleCount", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSampleCount(nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllCounterSamples", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSamples(nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

/*** COLLECTED METRIC SAMPLES ***/
TEST_CASE("astlGetMetricSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetMetricSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetMetricSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSampleCountOnTarget(nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSamplesOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSampleCount", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSampleCount(nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetAllMetricSamples", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSamples(nullptr, nullptr) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlTest", "[deprecated for now]") { REQUIRE(astlTest() == ASTL_STATUS_DEPRECATED_API); }
