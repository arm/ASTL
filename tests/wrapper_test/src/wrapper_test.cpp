#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/trompeloeil.hpp>
#include <chrono>
#include <cstdint>
#include <utility>

#include "../../mock_classes.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_test_hooks.h"
#include "astl_impl.hpp"
#include "counter.hpp"
#include "target.hpp"

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

auto MakeMinimalOrchestrator() -> std::pair<std::unique_ptr<astl::Orchestrator>, std::vector<expectation>> {
  auto                     topology_manager  = std::make_unique<MockTopologyManager>();
  auto                     collector_manager = std::make_unique<MockCollectorManager>();
  std::vector<expectation> expectations;
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  auto metric_manager = std::make_unique<MockMetricManager>();

  return {std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                               std::move(metric_manager)),
          std::move(expectations)};
}

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
  explicit TestOrchestratorInjector(std::unique_ptr<astl::Orchestrator> test_orchestrator) {
    astlInjectTestOrchestrator(test_orchestrator.release(), &_original_orchestrator);
  }

  /**
   * @brief swap the original orchestrator back into the library to resume use as normal
   */
  ~TestOrchestratorInjector() {
    // swap back the original orchestrator, and retrieve the test orchestrator for clean up.
    astl_test_orchestrator_t test_orchestrator_handle{nullptr};
    astlInjectTestOrchestrator(_original_orchestrator, &test_orchestrator_handle);
    // now clean up the `test_orchestrator` we received in this class's constructor
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete static_cast<astl::Orchestrator*>(test_orchestrator_handle);
  }

  // since we're managing a resource (an original orchestrator and test orchestrator),
  // disallow coppying and moving
  TestOrchestratorInjector(TestOrchestratorInjector const&)            = delete;
  TestOrchestratorInjector(TestOrchestratorInjector&&)                 = delete;
  TestOrchestratorInjector& operator=(TestOrchestratorInjector const&) = delete;
  TestOrchestratorInjector& operator=(TestOrchestratorInjector&&)      = delete;

 private:
  // hold the original orchestrator as a raw handle, since that's how the C interface provides it
  astl_test_orchestrator_t _original_orchestrator{nullptr};
};

// imprecise constants for testing
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

TEST_CASE("astl initialization macros") {
  ASTL_INIT_STRUCT(astl_initialization_parameters_t, init_params, ._configuration_file_path = nullptr);
  REQUIRE(init_params._size == sizeof(astl_initialization_parameters_t));

// For C++ client code, it is recommended to define your own template function to handle the initialization
// of a vector container.
// For testing this macro, We have a few warnings and linters to disable
// to use old-style casts and manual memory management.
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 26490)  // MSVC: "Don't use reinterpret_cast-style C-casts"
#elif defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
  // NOLINTNEXTLINE
  ASTL_ALLOC_ARRAY(astl_metric_properties_t, metric_properties, kAFew);
#ifdef _MSC_VER
#  pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

  REQUIRE(metric_properties != NULL);
  // NOLINTNEXTLINE
  ASTL_FREE_ARRAY(metric_properties);
  REQUIRE(metric_properties == NULL);
  // NOLINTNEXTLINE
  ASTL_FREE_ARRAY(metric_properties);
  REQUIRE(true);  // ensure no UB from double-free
}

TEST_CASE("astlInitialize checks for invalid input", "[wrapper_test]") {
  REQUIRE(astlInitialize(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetTargetCount", "[Reports 0 targets correctly]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t target_count{kJunk};
  REQUIRE(astlGetTargetCount(&target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == kAFew);
}

TEST_CASE("astlGetTargets", "[0 targets available]") {
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

TEST_CASE("astlGetTargets", "[Oversized buffer]") {
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

TEST_CASE("astlGetTargets", "[second target can't retrieve parameters]") {
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // get a handle back to the target
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{0};
  astlGetTargetCount(&target_count);
  REQUIRE(target_count == 1);
  astlGetTargets(targets.data(), &target_count);
  auto* target_handle = targets[0]._handle;

  SECTION("Request with oversized buffer") {
    uint32_t counter_count{kAFew};
    auto     counters = AllocateAstlVector<astl_counter_properties_t>(counter_count);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
    REQUIRE(counters[0]._mask == 0);
    REQUIRE(counters[1]._value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1]._counter_type == ASTL_COUNTER_TYPE_VALUE);
  }

  SECTION("Request with exact right buffer size") {
    uint32_t counter_count{2};
    auto     counters = AllocateAstlVector<astl_counter_properties_t>(counter_count);
    REQUIRE(astlGetCounters(target_handle, counters.data(), &counter_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(counters[0]._mask == 0);
    REQUIRE(counters[1]._value_type == ASTL_VALUE_FLOAT64);
    REQUIRE(counters[1]._counter_type == ASTL_COUNTER_TYPE_VALUE);
  }
}

TEST_CASE("astlGetMetrics", "[wrapper][Orchestrator]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));
  // set up metric
  auto metric_manager = std::make_unique<MockMetricManager>();
  auto mock_metric    = std::make_unique<MockMetric>();
  ALLOW_CALL(*mock_metric, GetProperties(_))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT64)
      .RETURN(ASTL_STATUS_SUCCESS);
  auto mock_metric2 = std::make_unique<MockMetric>();
  ALLOW_CALL(*mock_metric2, GetProperties(_))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT32)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::vector<astl::IMetric*> available_metrics;
  available_metrics.push_back(mock_metric.get());
  available_metrics.push_back(mock_metric2.get());

  ALLOW_CALL(*metric_manager, GetAvailableMetrics()).RETURN(std::span(available_metrics));
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                           std::move(metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("[bad params]") {
    REQUIRE(astlGetMetricCount(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricCount(mock_target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t metric_count{kJunk};
    REQUIRE(astlGetMetricCount(nullptr, &metric_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(metric_count == kJunk);
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(astlGetMetricCount(invalid_target_handle, &metric_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("[good params]") {
    uint32_t metric_count{kJunk};
    REQUIRE(astlGetMetricCount(mock_target_handle, &metric_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(metric_count == 2);
  }

  SECTION("astlGetMetrics", "[bad params]") {
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

  SECTION("astlGetMetrics", "[good params]") {
    uint32_t metric_count{2};
    auto     metrics = AllocateAstlVector<astl_metric_properties_t>(kAFew);
    REQUIRE(astlGetMetrics(mock_target_handle, metrics.data(), &metric_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(metrics[0]._value_type == ASTL_VALUE_FLOAT64);
  }
}

TEST_CASE("astlGetMetricGroupCount", "[unimplemented for now]") {
  uint32_t count{};
  REQUIRE(astlGetMetricGroupCount(nullptr, &count) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroups", "[unimplemented for now]") {
  std::array<astl_metric_group_properties_t, 1> properties{};
  uint32_t                                      count{};
  REQUIRE(astlGetMetricGroups(nullptr, properties.data(), &count) == ASTL_STATUS_NOT_IMPLEMENTED);
}

TEST_CASE("astlGetMetricGroupMetrics", "[unimplemented for now]") {
  std::array<astl_metric_properties_t, 1> properties{};
  REQUIRE(astlGetMetricGroupMetrics(nullptr, nullptr, properties.data()) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[Orchestrator]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));
  // set up metric
  auto                 metric_manager     = std::make_unique<MockMetricManager>();
  auto                 mock_metric        = std::make_unique<MockMetric>();
  astl_metric_handle_t mock_metric_handle = mock_metric.get();
  ALLOW_CALL(*mock_metric, GetProperties(_))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT64)
      .SIDE_EFFECT(_1->_handle = mock_metric_handle)
      .RETURN(ASTL_STATUS_SUCCESS);
  auto                 mock_metric2        = std::make_unique<MockMetric>();
  astl_metric_handle_t mock_metric_handle2 = mock_metric2.get();
  ALLOW_CALL(*mock_metric2, GetProperties(_))
      .SIDE_EFFECT(_1->_value_type = ASTL_VALUE_FLOAT32)
      .SIDE_EFFECT(_1->_handle = mock_metric_handle2)
      .RETURN(ASTL_STATUS_SUCCESS);

  std::vector<astl::IMetric*> available_metrics;
  available_metrics.push_back(mock_metric.get());
  available_metrics.push_back(mock_metric2.get());

  ALLOW_CALL(*metric_manager, GetAvailableMetrics()).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto* collector_manager_ptr_for_require_calls = collector_manager.get();
  auto  topology_manager                        = std::make_unique<MockTopologyManager>();
  auto  orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                            std::move(metric_manager));
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

  SECTION("[bad params]") {
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

  SECTION("[valid input]") {
    REQUIRE_CALL(*collector_manager_ptr_for_require_calls, ConfigureCollectionOnTarget(_, _, _))
        .RETURN(ASTL_STATUS_SUCCESS);
    REQUIRE(astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles.data(), 1) ==
            ASTL_STATUS_SUCCESS);
  }
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
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

TEST_CASE("astlConfigureCounterCollection", "[Test wrapper C->C++ wrapper code]") {
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

TEST_CASE("astlConfigureMetricCollectionOnTarget", "[bad parameters]") {
  // create mock target
  auto                                        mock_target_1 = std::make_unique<MockTarget>();
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target_1));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlConfigureMetricCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
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

TEST_CASE("astlReadImmediate", "[with 0 targets]") {
  // mock 0 targets
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
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
  auto topology_manager = std::make_unique<MockTopologyManager>();
  auto metric_manager   = std::make_unique<MockMetricManager>();
  auto orchestrator     = std::make_unique<astl::Orchestrator>(
      std::move(topology_manager), std::move(mock_collector_manager), std::move(metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astlReadImmediate() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlStartCollectionOnTarget", "[unimplemented for now]") {
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
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  // now that the test objects are in place, use the API as normal to get the handles to our objects
  auto     targets = AllocateAstlVector<astl_target_properties_t>(kAFew);
  uint32_t target_count{2};
  astlGetTargets(targets.data(), &target_count);
  int   junk{1};
  auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
  auto* working_target_handle{targets[0]._handle};
  auto* broken_target_handle{targets[1]._handle};

  auto     counters = AllocateAstlVector<astl_counter_properties_t>(kAFew);
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
  REQUIRE(astlGetCounterSamplesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetAllCounterSampleCountOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSampleCountOnTarget(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetAllCounterSamplesOnTarget", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSamplesOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetAllCounterSampleCount", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSampleCount(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetAllCounterSamples", "[unimplemented for now]") {
  REQUIRE(astlGetAllCounterSamples(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

/*** COLLECTED METRIC SAMPLES ***/
TEST_CASE("astlGetMetricSampleCountOnTarget", "[wrapper][Orchestrator]") {
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t sample_count{kJunk};
  auto     mock_metric = std::make_unique<MockMetric>();

  SECTION("[bad params]") {
    std::vector<astl::SampledData> samples;
    samples.emplace_back(1, astl::AstlValue{uint64_t{1}});
    samples.emplace_back(2, astl::AstlValue{uint64_t{2}});
    samples.emplace_back(3, astl::AstlValue{uint64_t{3}});
    ALLOW_CALL(*mock_metric, GetSamples()).RETURN(std::span<const astl::SampledData>{samples});

    // GetMetricSampleCount
    REQUIRE(astlGetMetricSampleCountOnTarget(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    int   junk{1};
    auto* invalid_target_handle{static_cast<astl_target_handle_t>(&junk)};
    auto  result = astlGetMetricSampleCountOnTarget(invalid_target_handle, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(astlGetMetricSampleCountOnTarget(mock_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricSampleCountOnTarget(mock_target_handle, nullptr, &sample_count) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricSampleCountOnTarget(mock_target_handle, mock_metric.get(), nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(sample_count == kJunk);
    // GetMetricSamples
    // invalid targets
    result = astlGetMetricSamplesOnTarget(nullptr, nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    result = astlGetMetricSamplesOnTarget(invalid_target_handle, nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), nullptr, nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), samples_out.data(), nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    sample_count = 1;  // too small a buffer - 3 samples are defined above
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL);
    sample_count = 0;  // 0 is not a valid size for the output buffer, even if 0 samples are expected
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_BAD_ARGUMENT);
    // ABI compatibility checks
    samples_out[0]._size = sizeof(astl_metric_sample_t) - 1;
    sample_count         = static_cast<uint32_t>(samples.size());
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION);
    samples_out[0]._size = sizeof(astl_metric_sample_t) + 1;
    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION);
  }

  SECTION("[no samples]") {
    REQUIRE_CALL(*mock_metric, GetSamples()).RETURN(std::span<const astl::SampledData>{});

    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);

    auto result = astlGetMetricSampleCountOnTarget(mock_target_handle, mock_metric.get(), &sample_count);
    REQUIRE((result == ASTL_STATUS_SUCCESS || result == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED));
    REQUIRE(sample_count == 0);
  }

  SECTION("[some samples]") {
    std::vector<astl::SampledData> samples;
    using std::chrono::microseconds;
    samples.emplace_back(1, astl::AstlValue{uint64_t{1}}, astl::SampleTimestamp{microseconds{100}});
    samples.emplace_back(2, astl::AstlValue{uint64_t{2}}, astl::SampleTimestamp{microseconds{101}});
    samples.emplace_back(3, astl::AstlValue{uint64_t{3}}, astl::SampleTimestamp{microseconds{102}});
    ALLOW_CALL(*mock_metric, GetSamples()).RETURN(std::span<const astl::SampledData>{samples});
    REQUIRE(astlGetMetricSampleCountOnTarget(mock_target_handle, mock_metric.get(), &sample_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == samples.size());

    sample_count     = 3;  // should match samples.size()};
    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);

    REQUIRE(astlGetMetricSamplesOnTarget(mock_target_handle, mock_metric.get(), samples_out.data(), &sample_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == samples.size());
    REQUIRE(samples_out[0]._value.ui64 == 1);
    REQUIRE(samples_out[0]._timestamp == 100);
    REQUIRE(samples_out[1]._value.ui64 == 2);
    REQUIRE(samples_out[1]._timestamp == 101);
    REQUIRE(samples_out[2]._value.ui64 == 3);
    REQUIRE(samples_out[2]._timestamp == 102);
  }
}

TEST_CASE("astlGetAllMetricSamplesOnTarget", "[wrapper][Orchestrator]") {
  // The topology for this test is 1 target with 3 metrics.
  // The first metric has 0 samples, the second metric has 1 sample, and the third metric has 3 samples.
  // set up a test orchestrator with 1 mock target
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  // now set up some fake metrics and samples
  auto mock_metric0 = std::make_unique<MockMetric>();
  auto mock_metric1 = std::make_unique<MockMetric>();
  auto mock_metric2 = std::make_unique<MockMetric>();
  using std::chrono::microseconds;
  std::vector<astl::SampledData> samples1;
  samples1.emplace_back(1, astl::AstlValue{uint64_t{1}}, astl::SampleTimestamp{microseconds{101}});
  std::vector<astl::SampledData> samples2;
  samples2.emplace_back(2, astl::AstlValue{uint64_t{2}}, astl::SampleTimestamp{microseconds{102}});
  samples2.emplace_back(3, astl::AstlValue{uint64_t{3}}, astl::SampleTimestamp{microseconds{103}});
  samples2.emplace_back(4, astl::AstlValue{uint64_t{4}}, astl::SampleTimestamp{microseconds{104}});
  ALLOW_CALL(*mock_metric0, GetSamples()).RETURN(std::span<const astl::SampledData>{});
  ALLOW_CALL(*mock_metric1, GetSamples()).RETURN(std::span<const astl::SampledData>{samples1});
  ALLOW_CALL(*mock_metric2, GetSamples()).RETURN(std::span<const astl::SampledData>{samples2});

  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  auto metrics_pointers    = std::vector<astl::IMetric*>{mock_metric0.get(), mock_metric1.get(), mock_metric2.get()};
  ALLOW_CALL(*mock_metric_manager, GetAvailableMetrics()).RETURN(metrics_pointers);
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                           std::move(mock_metric_manager));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t sample_count{kJunk};

  SECTION("astlGetAllMetricSampleCountOnTarget [bad params]") {
    REQUIRE(astlGetAllMetricSampleCountOnTarget(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    int                  junk{1};
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    auto                 result                = astlGetAllMetricSampleCountOnTarget(invalid_target_handle, nullptr);
    REQUIRE((result == ASTL_STATUS_INVALID_TARGET_HANDLE || result == ASTL_STATUS_BAD_ARGUMENT));
    REQUIRE(astlGetAllMetricSampleCountOnTarget(invalid_target_handle, &sample_count) ==
            ASTL_STATUS_INVALID_TARGET_HANDLE);
    REQUIRE(astlGetAllMetricSampleCountOnTarget(mock_target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("astlGetAllMetricSampleCountOnTarget [good params]") {
    REQUIRE(astlGetAllMetricSampleCountOnTarget(mock_target_handle, &sample_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(sample_count == 4);  // 0 for metric0, 1 for metric1, and 3 for metric2
  }

  SECTION("astlGetAllMetricSamplesOnTarget [bad params]") {
    auto result = astlGetAllMetricSamplesOnTarget(nullptr, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_BAD_ARGUMENT || result == ASTL_STATUS_INVALID_TARGET_HANDLE));
    int                  junk{1};
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    result = astlGetAllMetricSamplesOnTarget(invalid_target_handle, nullptr, nullptr);
    REQUIRE((result == ASTL_STATUS_BAD_ARGUMENT || result == ASTL_STATUS_INVALID_TARGET_HANDLE));
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, samples_out.data(), nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    sample_count = 0;  // 0 is an invalid buffer size - we need to check one element's _size field for version
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, samples_out.data(), &sample_count) ==
            ASTL_STATUS_BAD_ARGUMENT);
    sample_count = 1;  // 1 is a valid buffer size - but too small to hold our samples
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, samples_out.data(), &sample_count) ==
            ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL);
    sample_count = 4;
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, nullptr, &sample_count) == ASTL_STATUS_BAD_ARGUMENT);
    samples_out[0]._size = sizeof(astl_metric_sample_t) - 1;
    sample_count         = 4;
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, samples_out.data(), &sample_count) ==
            ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION);
    samples_out[0]._size = sizeof(astl_metric_sample_t) + 1;
    sample_count         = 4;
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, samples_out.data(), &sample_count) ==
            ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION);
  }

  SECTION("astlGetAllMetricSamplesOnTarget [good params]") {
    auto samples_out = AllocateAstlVector<astl_metric_sample_t>(kAFew);
    sample_count     = 4;
    REQUIRE(astlGetAllMetricSamplesOnTarget(mock_target_handle, samples_out.data(), &sample_count) ==
            ASTL_STATUS_SUCCESS);

    // check for all 4 samples, not necessarily in order
    auto is_matching_sample = [](const astl_metric_sample_t& sample, uint64_t value, uint64_t timestamp) {
      return sample._value.ui64 == value && sample._timestamp == timestamp;
    };
    REQUIRE(samples_out.end() != std::find_if(samples_out.begin(), samples_out.end(),
                                              std::bind(is_matching_sample, std::placeholders::_1, 1, 101)));
    REQUIRE(samples_out.end() != std::find_if(samples_out.begin(), samples_out.end(),
                                              std::bind(is_matching_sample, std::placeholders::_1, 2, 102)));
    REQUIRE(samples_out.end() != std::find_if(samples_out.begin(), samples_out.end(),
                                              std::bind(is_matching_sample, std::placeholders::_1, 3, 103)));
    REQUIRE(samples_out.end() != std::find_if(samples_out.begin(), samples_out.end(),
                                              std::bind(is_matching_sample, std::placeholders::_1, 4, 104)));
  }
}

TEST_CASE("astlGetAllMetricSamples", "[unimplemented for now]") {
  REQUIRE(astlGetAllMetricSamples(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}
