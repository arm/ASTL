#include <catch2/catch_test_macros.hpp>
#include <trompeloeil.hpp>

#include "../../mock_classes.hpp"
#include "astl/astl.h"
#include "astl_impl.hpp"
#include "common/i_sample_sink.hpp"

using trompeloeil::_;

TEST_CASE("Orchestrator.Test()", "[is deprecated][Orchestrator]") {
  REQUIRE(astl::Orchestrator::GetInstance()->Test() == ASTL_STATUS_DEPRECATED_API);
}

TEST_CASE("Orchestrator-Collection", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  // ALLOW_CALL(*topology_manager, SetTargets(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  auto orchestrator =
      astl::Orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager));

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
  ALLOW_CALL(*collector_manager, RegisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, ProcessData(_)).RETURN(ASTL_STATUS_COLLECTION_ALREADY_STOPPED);

  auto topology_manager = std::make_unique<MockTopologyManager>();
  REQUIRE(topology_manager->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  REQUIRE(topology_manager->GetTargets().size() == 1);
  SECTION("No metric manager") {
    astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), nullptr);
    REQUIRE(orchestrator.GetTargets().size() == 1);
    const std::unique_ptr<astl::ITarget>& target_ptr = orchestrator.GetTargets()[0];
    REQUIRE(orchestrator.StopCollection(target_ptr.get()) == ASTL_STATUS_INTERNAL_ERROR);
  }

  SECTION("No collector manager") {
    astl::Orchestrator orchestrator(std::move(topology_manager),  // cppcheck-suppress accessMoved
                                    nullptr,
                                    std::move(metric_manager));  // cppcheck-suppress accessMoved
    const auto&        target_ptr = orchestrator.GetTargets()[0];
    REQUIRE(orchestrator.StopCollection(target_ptr.get()) == ASTL_STATUS_INTERNAL_ERROR);
  }

  SECTION("valid collector and metrics") {
    astl::Orchestrator orchestrator(std::move(topology_manager),   // cppcheck-suppress accessMoved
                                    std::move(collector_manager),  // cppcheck-suppress accessMoved
                                    std::move(metric_manager));    // cppcheck-suppress accessMoved
    const auto&        target_ptr = orchestrator.GetTargets()[0];
    REQUIRE(orchestrator.StopCollection(target_ptr.get()) == ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
  }
}

TEST_CASE("Orchestrator-SinkSamples", "[Orchestrator]") {
  auto topology_manager = std::make_unique<MockTopologyManager>();
  auto orchestrator     = astl::Orchestrator(std::move(topology_manager), nullptr, nullptr);

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
}
