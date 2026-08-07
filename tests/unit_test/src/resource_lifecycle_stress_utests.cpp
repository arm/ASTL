// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../resource_lifecycle_stress_utils.hpp"
#include "../../test_includes.hpp"
#include "astl/astl_errors.h"
#include "operation/operation.hpp"
#include "orchestrator/orchestrator.hpp"

using astl::testing::KeepExpectationsAlive;
using astl::testing::kResourceLifecycleStressIterations;
using astl::testing::MakeMetricLifecycleCollectionExpectations;
using astl::testing::MakeSingleSampleOperation;
using astl::testing::MakeTestClockCorrelations;
using trompeloeil::_;

namespace {
struct TestOperation : astl::Operation {};

struct TrackingCollectorManager : MockCollectorManager {
  auto ClearConfiguredCollections() -> astl_status_code override {
    ++clear_configured_collection_calls;
    return clear_configured_collection_status;
  }

  int              clear_configured_collection_calls{0};
  astl_status_code clear_configured_collection_status{ASTL_STATUS_SUCCESS};
};
}  // namespace

TEST_CASE("Operation ID allocator saturates and recovers after reset", "[stress][lifecycle][operation]") {
  astl::Operation::ResetOperationIdAllocator();

  std::vector<TestOperation> operations;
  operations.reserve(astl::kOperationIdInvalid - astl::kFirstAssignableOperationId);
  for (auto expected_id = astl::kFirstAssignableOperationId; expected_id < astl::kOperationIdInvalid; ++expected_id) {
    operations.emplace_back();
    REQUIRE(operations.back().GetId() == expected_id);
  }

  REQUIRE_THROWS_AS(TestOperation{}, astl::OperationIdExhausted);

  astl::Operation::ResetOperationIdAllocator();
  REQUIRE(TestOperation{}.GetId() == astl::kFirstAssignableOperationId);
}

TEST_CASE("Orchestrator singleton tolerates repeated initialize and reset", "[stress][lifecycle][Orchestrator]") {
  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    astl::Orchestrator::ResetInstance();
    REQUIRE_FALSE(astl::Orchestrator::IsInitialized());

    auto topology_manager  = std::make_unique<MockTopologyManager>();
    auto collector_manager = std::make_unique<MockCollectorManager>();
    ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
    ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

    auto metric_manager = std::make_unique<MockMetricManager>();
    ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
    ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

    auto output_manager = std::make_unique<MockOutputManager>();

    astl::Orchestrator::InitializeInstance(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), "");
    REQUIRE(astl::Orchestrator::IsInitialized());
    REQUIRE(astl::Orchestrator::GetInstance().has_value());

    astl::Orchestrator::ResetInstance();
    REQUIRE_FALSE(astl::Orchestrator::IsInitialized());
  }
}

TEST_CASE("Orchestrator tolerates repeated configure/start/pause/resume/stop cleanup",
          "[stress][lifecycle][Orchestrator]") {
  using State = astl::Orchestrator::TargetCollectionState;
  astl::Operation::ResetOperationIdAllocator();

  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<TrackingCollectorManager>();
  auto* collector_raw     = collector_manager.get();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, GetNativeClockSnapshot(_)).RETURN(MakeTestClockCorrelations());
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ReadImmediateOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto metric_expectations = MakeMetricLifecycleCollectionExpectations(*metric_manager, available_metrics);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 target        = std::make_unique<MockTarget>();
  astl_target_handle_t target_handle = target.get();
  ALLOW_CALL(*target, GetProperties(_)).SIDE_EFFECT(_1->handle = target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_name = "resource_lifecycle_stress_target";
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target_raw = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size = sizeof(params);
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(orchestrator.ConfigureMetricCollection(target_raw, &params, metrics) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::CONFIGURED);

    REQUIRE(orchestrator.ReadImmediate(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.StartCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::STARTED);

    REQUIRE(orchestrator.PauseCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::PAUSED);

    REQUIRE(orchestrator.ResumeCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::STARTED);

    REQUIRE(orchestrator.StopCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::STOPPED);

    REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::UNCONFIGURED);
  }

  REQUIRE(collector_raw->clear_configured_collection_calls >= kResourceLifecycleStressIterations);
  KeepExpectationsAlive(metric_expectations);
}

TEST_CASE("Orchestrator tolerates repeated multi-target metric lifecycle churn",
          "[stress][lifecycle][Orchestrator][multi-target]") {
  using State = astl::Orchestrator::TargetCollectionState;
  astl::Operation::ResetOperationIdAllocator();

  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<TrackingCollectorManager>();
  auto* collector_raw     = collector_manager.get();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, GetNativeClockSnapshot(_)).RETURN(MakeTestClockCorrelations());
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ReadImmediateOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto metric_expectations = MakeMetricLifecycleCollectionExpectations(*metric_manager, available_metrics);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  std::array<std::string, 3>                  target_names{"multi_target_0", "multi_target_1", "multi_target_2"};
  std::vector<const astl::ITarget*>           target_raws;
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  target_raws.reserve(target_names.size());
  targets.reserve(target_names.size());

  for (const auto& target_name : target_names) {
    auto  target     = std::make_unique<TestTargetBase>(target_name, astl::CollectorType::UNKNOWN);
    auto* target_raw = target.get();
    target_raws.push_back(target_raw);
    targets.push_back(std::move(target));
  }
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl_collection_params_t params{};
  params.size = sizeof(params);
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    for (const auto* target : target_raws) {
      REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);
      REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);
    }
    for (const auto* target : target_raws) {
      REQUIRE(orchestrator.ReadImmediate(target) == ASTL_STATUS_SUCCESS);
      REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_SUCCESS);
      REQUIRE(orchestrator.PauseCollection(target) == ASTL_STATUS_SUCCESS);
      REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_SUCCESS);
      REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
      REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::STOPPED);
    }
    REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_SUCCESS);
  }

  REQUIRE(collector_raw->clear_configured_collection_calls >= kResourceLifecycleStressIterations);
  KeepExpectationsAlive(metric_expectations);
}

TEST_CASE("Orchestrator tolerates repeated counter collection lifecycle churn",
          "[stress][lifecycle][Orchestrator][counter]") {
  using State = astl::Orchestrator::TargetCollectionState;
  astl::Operation::ResetOperationIdAllocator();

  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<TrackingCollectorManager>();
  auto* collector_raw     = collector_manager.get();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, GetNativeClockSnapshot(_)).RETURN(MakeTestClockCorrelations());
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                               metric_manager        = std::make_unique<MockMetricManager>();
  static int                         dummy_counter_storage = 0;
  astl_counter_handle_t              counter_handle        = &dummy_counter_storage;
  std::vector<astl_counter_handle_t> available_counters{counter_handle};
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableCounters(_))
      .RETURN(std::expected<std::span<const astl_counter_handle_t>, astl_status_code>{available_counters});
  ALLOW_CALL(*metric_manager, GetCounterRequiredOperations(_, _)).LR_RETURN(MakeSingleSampleOperation());
  ALLOW_CALL(*metric_manager, SetClockCorrelations(_));
  ALLOW_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 target        = std::make_unique<MockTarget>();
  astl_target_handle_t target_handle = target.get();
  ALLOW_CALL(*target, GetProperties(_)).SIDE_EFFECT(_1->handle = target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_name = "counter_resource_lifecycle_stress_target";
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target_raw = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size = sizeof(params);
  std::array<astl_counter_handle_t, 1> counters{counter_handle};

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(orchestrator.ConfigureCounterCollection(target_raw, &params, counters) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::CONFIGURED);
    REQUIRE(orchestrator.StartCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.StopCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::STOPPED);
    REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_SUCCESS);
  }

  REQUIRE(collector_raw->clear_configured_collection_calls >= kResourceLifecycleStressIterations);
}

TEST_CASE("Orchestrator recovers collection state after repeated start failures",
          "[stress][lifecycle][Orchestrator][failure]") {
  using State = astl::Orchestrator::TargetCollectionState;
  astl::Operation::ResetOperationIdAllocator();

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<TrackingCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, GetNativeClockSnapshot(_)).RETURN(MakeTestClockCorrelations());
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_INTERNAL_ERROR);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto metric_expectations = MakeMetricLifecycleCollectionExpectations(*metric_manager, available_metrics);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 target        = std::make_unique<MockTarget>();
  astl_target_handle_t target_handle = target.get();
  ALLOW_CALL(*target, GetProperties(_)).SIDE_EFFECT(_1->handle = target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_name = "start_failure_lifecycle_stress_target";
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target_raw = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size = sizeof(params);
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(orchestrator.ConfigureMetricCollection(target_raw, &params, metrics) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.StartCollection(target_raw) == ASTL_STATUS_INTERNAL_ERROR);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::CONFIGURED);
    REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_SUCCESS);
  }
  KeepExpectationsAlive(metric_expectations);
}

TEST_CASE("Orchestrator remains stoppable after repeated pause failures",
          "[stress][lifecycle][Orchestrator][failure]") {
  using State = astl::Orchestrator::TargetCollectionState;
  astl::Operation::ResetOperationIdAllocator();

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<TrackingCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, GetNativeClockSnapshot(_)).RETURN(MakeTestClockCorrelations());
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_INTERNAL_ERROR);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto metric_expectations = MakeMetricLifecycleCollectionExpectations(*metric_manager, available_metrics);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 target        = std::make_unique<MockTarget>();
  astl_target_handle_t target_handle = target.get();
  ALLOW_CALL(*target, GetProperties(_)).SIDE_EFFECT(_1->handle = target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_name = "pause_failure_lifecycle_stress_target";
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target_raw = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size = sizeof(params);
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(orchestrator.ConfigureMetricCollection(target_raw, &params, metrics) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.StartCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.PauseCollection(target_raw) == ASTL_STATUS_INTERNAL_ERROR);
    REQUIRE(orchestrator.GetTargetCollectionState(target_raw).value() == State::STARTED);
    REQUIRE(orchestrator.StopCollection(target_raw) == ASTL_STATUS_SUCCESS);
    REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_SUCCESS);
  }
  KeepExpectationsAlive(metric_expectations);
}

TEST_CASE("Orchestrator remains reusable after repeated failed cleanup attempts", "[stress][lifecycle][Orchestrator]") {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<TrackingCollectorManager>();
  auto* collector_raw     = collector_manager.get();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  collector_raw->clear_configured_collection_status = ASTL_STATUS_FILE_ERROR;
  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_FILE_ERROR);
  }

  collector_raw->clear_configured_collection_status = ASTL_STATUS_SUCCESS;
  REQUIRE(orchestrator.ResetCollectionStateForCleanConfigure() == ASTL_STATUS_SUCCESS);
}
