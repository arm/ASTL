// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "../../resource_lifecycle_stress_utils.hpp"
#include "wrapper_utils.hpp"

using astl::testing::KeepExpectationsAlive;
using astl::testing::kResourceLifecycleStressIterations;
using astl::testing::MakeMetricLifecycleCollectionExpectations;
using astl::testing::MakeTestClockCorrelations;
using trompeloeil::_;

namespace {
auto RepeatedInvalidTargetCallsReturnBadArgument(const astl_read_immediate_on_target_params_t&    read_params,
                                                 const astl_start_collection_on_target_params_t&  start_params,
                                                 const astl_pause_collection_on_target_params_t&  pause_params,
                                                 const astl_resume_collection_on_target_params_t& resume_params,
                                                 const astl_stop_collection_on_target_params_t&   stop_params) -> bool {
  if (astlReadImmediateOnTarget(&read_params) != ASTL_STATUS_BAD_ARGUMENT) {
    return false;
  }
  if (astlStartCollectionOnTarget(&start_params) != ASTL_STATUS_BAD_ARGUMENT) {
    return false;
  }
  if (astlPauseCollectionOnTarget(&pause_params) != ASTL_STATUS_BAD_ARGUMENT) {
    return false;
  }
  if (astlResumeCollectionOnTarget(&resume_params) != ASTL_STATUS_BAD_ARGUMENT) {
    return false;
  }
  if (astlStopCollectionOnTarget(&stop_params) != ASTL_STATUS_BAD_ARGUMENT) {
    return false;
  }
  return true;
}
}  // namespace

TEST_CASE("ASTL wrapper APIs tolerate repeated target lifecycle calls", "[wrapper][stress][lifecycle]") {
  astl::Operation::ResetOperationIdAllocator();

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, GetNativeClockSnapshot(_)).RETURN(MakeTestClockCorrelations());
  ALLOW_CALL(*collector_manager, ReadImmediateOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  auto metric_expectations = MakeMetricLifecycleCollectionExpectations(*metric_manager, available_metrics);

  auto output_manager = std::make_unique<MockOutputManager>();

  auto  target     = std::make_unique<MockTarget>();
  auto* target_raw = target.get();
  ALLOW_CALL(*target, GetProperties(_)).SIDE_EFFECT(_1->handle = target_raw).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  static const std::string target_name = "wrapper_resource_lifecycle_stress_target";
  ALLOW_CALL(*target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                           std::move(metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  astl_collection_params_t collection_params{};
  collection_params.size = sizeof(collection_params);
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  astl_configure_metric_collection_on_target_params_t configure_params{};
  configure_params.size              = sizeof(configure_params);
  configure_params.target_handle     = target_raw;
  configure_params.collection_params = &collection_params;
  configure_params.metric_handles    = metrics.data();
  configure_params.metric_count      = static_cast<uint32_t>(metrics.size());

  astl_read_immediate_on_target_params_t read_params{};
  read_params.size          = sizeof(read_params);
  read_params.target_handle = target_raw;

  astl_start_collection_on_target_params_t start_params{};
  start_params.size          = sizeof(start_params);
  start_params.target_handle = target_raw;

  astl_pause_collection_on_target_params_t pause_params{};
  pause_params.size          = sizeof(pause_params);
  pause_params.target_handle = target_raw;

  astl_resume_collection_on_target_params_t resume_params{};
  resume_params.size          = sizeof(resume_params);
  resume_params.target_handle = target_raw;

  astl_stop_collection_on_target_params_t stop_params{};
  stop_params.size          = sizeof(stop_params);
  stop_params.target_handle = target_raw;

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(astlConfigureMetricCollectionOnTarget(&configure_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(astlReadImmediateOnTarget(&read_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(astlStartCollectionOnTarget(&start_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(astlPauseCollectionOnTarget(&pause_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(astlResumeCollectionOnTarget(&resume_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(astlStopCollectionOnTarget(&stop_params) == ASTL_STATUS_SUCCESS);
  }
  KeepExpectationsAlive(metric_expectations);
}

TEST_CASE("ASTL wrapper validation paths tolerate repeated failure calls", "[wrapper][stress][lifecycle][failure]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  astl_read_immediate_on_target_params_t read_params{};
  read_params.size = sizeof(read_params);
  astl_start_collection_on_target_params_t start_params{};
  start_params.size = sizeof(start_params);
  astl_pause_collection_on_target_params_t pause_params{};
  pause_params.size = sizeof(pause_params);
  astl_resume_collection_on_target_params_t resume_params{};
  resume_params.size = sizeof(resume_params);
  astl_stop_collection_on_target_params_t stop_params{};
  stop_params.size = sizeof(stop_params);

  for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
    CAPTURE(iteration);
    REQUIRE(astlReadImmediateOnTarget(&read_params) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlStartCollectionOnTarget(&start_params) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlPauseCollectionOnTarget(&pause_params) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlResumeCollectionOnTarget(&resume_params) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlStopCollectionOnTarget(&stop_params) == ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("ASTL wrapper validation paths tolerate concurrent repeated failure calls",
          "[wrapper][stress][lifecycle][thread_safety][failure]") {
  auto [orchestrator, expectations] = MakeMinimalOrchestrator();
  TestOrchestratorInjector injector(std::move(orchestrator));

  constexpr int            k_thread_count = 4;
  std::atomic<bool>        all_ok{true};
  std::atomic<int>         ready_count{0};
  std::atomic<bool>        start{false};
  std::vector<std::thread> workers;
  workers.reserve(k_thread_count);

  for (int thread_index = 0; thread_index < k_thread_count; ++thread_index) {
    workers.emplace_back([&all_ok, &ready_count, &start]() {
      ++ready_count;
      while (!start.load(std::memory_order_acquire)) {
      }

      astl_read_immediate_on_target_params_t read_params{};
      read_params.size = sizeof(read_params);
      astl_start_collection_on_target_params_t start_params{};
      start_params.size = sizeof(start_params);
      astl_pause_collection_on_target_params_t pause_params{};
      pause_params.size = sizeof(pause_params);
      astl_resume_collection_on_target_params_t resume_params{};
      resume_params.size = sizeof(resume_params);
      astl_stop_collection_on_target_params_t stop_params{};
      stop_params.size = sizeof(stop_params);

      for (int iteration = 0; iteration < kResourceLifecycleStressIterations; ++iteration) {
        if (!RepeatedInvalidTargetCallsReturnBadArgument(read_params, start_params, pause_params, resume_params,
                                                         stop_params)) {
          all_ok.store(false, std::memory_order_release);
          break;
        }
      }
    });
  }

  while (ready_count.load(std::memory_order_acquire) < k_thread_count) {
  }
  start.store(true, std::memory_order_release);

  for (auto& worker : workers) {
    worker.join();
  }

  REQUIRE(all_ok.load(std::memory_order_acquire));
}
