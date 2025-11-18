#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_test_hooks.h"
#include "common/metric_config.hpp"
#include "metric/counter.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"

template <typename T>
auto AllocateAstlVector(size_t count) -> std::vector<T> {
  std::vector<T> objects{count};
  if (count > 0) {
    objects[0]._size = sizeof(T);
  }
  return objects;
}

using expectation = std::unique_ptr<trompeloeil::expectation>;

inline auto MakeMinimalOrchestrator() -> std::pair<std::unique_ptr<astl::Orchestrator>, std::vector<expectation>> {
  using trompeloeil::_;
  auto                     topology_manager  = std::make_unique<MockTopologyManager>();
  auto                     collector_manager = std::make_unique<MockCollectorManager>();
  std::vector<expectation> expectations;
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  auto metric_manager = std::make_unique<MockMetricManager>();
  expectations.push_back(
      NAMED_ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  auto output_manager = std::make_unique<MockOutputManager>();

  return {std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                               std::move(metric_manager), std::move(output_manager)),
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