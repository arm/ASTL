#include <stdexcept>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl_impl.hpp"
#include "common/i_raw_sample_sink.hpp"

using Catch::Matchers::ContainsSubstring;
using trompeloeil::_;

TEST_CASE("Orchestrator ctor", "[Orchestrator]") {
  // configure managers
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
  auto output_manager   = std::make_unique<MockOutputManager>();
  auto topology_manager = std::make_unique<MockTopologyManager>();

  SECTION("All nullptrs") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(nullptr, nullptr, nullptr, nullptr), std::invalid_argument,
                           MessageMatches(ContainsSubstring("requires non-null")));
  }

  SECTION("null topology_manager") {
    REQUIRE_THROWS_MATCHES(
        // since the 'SECTION' macros break the test case up into independent runs,
        // we're not _actually_ moving the same variables like collector-manager multiple times,
        // even though it looks like that syntactically. cppcheck can't properly expand the `SECTION` macro,
        // so we'll suppress the (moving a moved-from variable) warning here.
        // cppcheck-suppress accessMoved
        astl::Orchestrator(nullptr, std::move(collector_manager), std::move(metric_manager), std::move(output_manager)),
        std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }
  // cppcheck-suppress-begin accessMoved
  SECTION("null collector_manager") {
    REQUIRE_THROWS_MATCHES(
        astl::Orchestrator(std::move(topology_manager), nullptr, std::move(metric_manager), std::move(output_manager)),
        std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }

  SECTION("null metric_manager") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(std::move(topology_manager), std::move(collector_manager), nullptr,
                                              std::move(output_manager)),
                           std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }

  SECTION("null output_manager") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                              std::move(metric_manager), nullptr),
                           std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }
  // cppcheck-suppress-end accessMoved
}

TEST_CASE("Orchestrator-Collection", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  // ALLOW_CALL(*topology_manager, SetTargets(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager));

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  orchestrator.SetTargets(std::move(mock_targets));

  auto unexpected_target = std::make_unique<MockTarget>();

  SECTION("PauseCollection", "[invalid-parameters]") {
    REQUIRE(orchestrator.PauseCollection(nullptr) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    REQUIRE(orchestrator.PauseCollection(unexpected_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("ResumeCollection", "[invalid-parameters]") {
    REQUIRE(orchestrator.ResumeCollection(nullptr) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    REQUIRE(orchestrator.ResumeCollection(unexpected_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("StopCollection", "[invalid-parameters]") {
    REQUIRE(orchestrator.StopCollection(nullptr) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    REQUIRE(orchestrator.StopCollection(unexpected_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
}

TEST_CASE("Orchestrator-StopCollection", "[Orchestrator]") {
  //  start up targets
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  // configure managers
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_COLLECTION_ALREADY_STOPPED);

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();

  auto topology_manager = std::make_unique<MockTopologyManager>();
  REQUIRE(topology_manager->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  REQUIRE(topology_manager->GetTargets().size() == 1);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));
  const auto&        target_ptr = orchestrator.GetTargets()[0];
  REQUIRE(orchestrator.StopCollection(target_ptr.get()) == ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
}

TEST_CASE("Orchestrator-SinkRawSamples", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager));

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
}
