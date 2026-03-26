// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <fstream>
#include <memory>
#include <stdexcept>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_test_hooks.h"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "serdes/archive_utils.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "target.hpp"
#include "topology/topology_manager.hpp"

using Catch::Matchers::ContainsSubstring;
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

TEST_CASE("Orchestrator ctor", "[Orchestrator]") {
  // configure managers
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
  auto output_manager   = std::make_unique<MockOutputManager>();
  auto topology_manager = std::make_unique<MockTopologyManager>();

  SECTION("All nullptrs") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(nullptr, nullptr, nullptr, nullptr, ""), std::invalid_argument,
                           MessageMatches(ContainsSubstring("requires non-null")));
  }

  SECTION("null topology_manager") {
    REQUIRE_THROWS_MATCHES(
        // since the 'SECTION' macros break the test case up into independent runs,
        // we're not _actually_ moving the same variables like collector-manager multiple times,
        // even though it looks like that syntactically. cppcheck can't properly expand the `SECTION` macro,
        // so we'll suppress the (moving a moved-from variable) warning here.
        // cppcheck-suppress accessMoved
        astl::Orchestrator(nullptr, std::move(collector_manager), std::move(metric_manager), std::move(output_manager),
                           ""),
        std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }
  // cppcheck-suppress-begin accessMoved
  SECTION("null collector_manager") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(std::move(topology_manager), nullptr, std::move(metric_manager),
                                              std::move(output_manager), ""),
                           std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }

  SECTION("null metric_manager") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(std::move(topology_manager), std::move(collector_manager), nullptr,
                                              std::move(output_manager), ""),
                           std::invalid_argument, MessageMatches(ContainsSubstring("requires non-null")));
  }

  SECTION("null output_manager") {
    REQUIRE_THROWS_MATCHES(astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                              std::move(metric_manager), nullptr, ""),
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
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_handle; _1->name = "mock_target")
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

  auto unexpected_target = std::make_unique<MockTarget>();

  SECTION("PauseCollection", "[invalid-parameters]") {
    REQUIRE(orchestrator.PauseCollection(nullptr) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    REQUIRE(orchestrator.PauseCollection(unexpected_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("Pause/Resume lifecycle") {
    // Acquire target
    const auto& targets = orchestrator.GetTargets();
    REQUIRE(targets.size() == 1);
    auto* target = targets[0].get();

    // Pausing before start -> not running
    REQUIRE(orchestrator.PauseCollection(target) == ASTL_STATUS_COLLECTION_NOT_RUNNING);
    // Resuming before pause -> not paused
    REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_COLLECTION_NOT_PAUSED);

    // Not wiring a full re-init; rely on existing mock expectations if any
    // Start should fail if not configured; expect COLLECTION_NOT_CONFIGURED
    REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
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

TEST_CASE("Orchestrator-BulkStateQuery", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  // Lifecycle operations expectations
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  // Insert a single target
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  // Initial state snapshot
  auto states = orchestrator.GetAllTargetCollectionStates();
  REQUIRE(states.size() == 1);
  auto it = states.find(target);
  REQUIRE(it != states.end());
  REQUIRE(it->second == astl::Orchestrator::TargetCollectionState::UNCONFIGURED);

  // Configure metric collection (will transition to CONFIGURED)
  // Provide minimal dummy configuration inputs
  // NOTE: Configuration path not yet producing CONFIGURED state via public metric API in tests.
  // Since ConfigureMetricCollection currently performs validation without real metrics, force state change manually
  // by emulating collector configuration success path via direct map update through public API call we have.
  // Use ConfigureMetricCollection with empty metrics; expect METRIC_NOT_SUPPORTED or INTERNAL errors -> we cannot rely.
  // Instead, simulate configuration by calling StartCollection (will fail) is not enough; directly test states after
  // manual insertion not possible. So skip to start attempt which should yield COLLECTION_NOT_CONFIGURED and leave
  // state UNCONFIGURED.
  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);

  // Manually emulate configuration via internal map is not exposed; so we limit test to validating snapshot updates
  // after pause/resume attempts yield no transitions.
  states = orchestrator.GetAllTargetCollectionStates();
  it     = states.find(target);
  REQUIRE(it != states.end());
  REQUIRE(it->second == astl::Orchestrator::TargetCollectionState::UNCONFIGURED);

  // Force pause should return NOT_RUNNING and not change state
  REQUIRE(orchestrator.PauseCollection(target) == ASTL_STATUS_COLLECTION_NOT_RUNNING);
  states = orchestrator.GetAllTargetCollectionStates();
  it     = states.find(target);
  REQUIRE(it != states.end());
  REQUIRE(it->second == astl::Orchestrator::TargetCollectionState::UNCONFIGURED);

  // Force resume should return NOT_PAUSED and not change state
  REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_COLLECTION_NOT_PAUSED);
  states = orchestrator.GetAllTargetCollectionStates();
  it     = states.find(target);
  REQUIRE(it != states.end());
  REQUIRE(it->second == astl::Orchestrator::TargetCollectionState::UNCONFIGURED);
}

TEST_CASE("Orchestrator-TargetCollectionStateToString", "[Orchestrator]") {
  using astl::Orchestrator;
  REQUIRE(std::string(Orchestrator::TargetCollectionStateToString(Orchestrator::TargetCollectionState::UNCONFIGURED)) ==
          "UNCONFIGURED");
  REQUIRE(std::string(Orchestrator::TargetCollectionStateToString(Orchestrator::TargetCollectionState::CONFIGURED)) ==
          "CONFIGURED");
  REQUIRE(std::string(Orchestrator::TargetCollectionStateToString(Orchestrator::TargetCollectionState::STARTING)) ==
          "STARTING");
  REQUIRE(std::string(Orchestrator::TargetCollectionStateToString(Orchestrator::TargetCollectionState::STARTED)) ==
          "STARTED");
  REQUIRE(std::string(Orchestrator::TargetCollectionStateToString(Orchestrator::TargetCollectionState::PAUSED)) ==
          "PAUSED");
  REQUIRE(std::string(Orchestrator::TargetCollectionStateToString(Orchestrator::TargetCollectionState::STOPPED)) ==
          "STOPPED");
}

TEST_CASE("Orchestrator-StopCollection", "[Orchestrator]") {
  //  start up targets
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_handle; _1->name = "mock_target")
      .RETURN(ASTL_STATUS_SUCCESS);
  static const std::string name = "mock_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(name);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));

  // configure managers
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_COLLECTION_ALREADY_STOPPED);

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();

  auto topology_manager = std::make_unique<MockTopologyManager>();
  REQUIRE(topology_manager->SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  REQUIRE(topology_manager->GetTargets().size() == 1);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  const auto&        target_ptr = orchestrator.GetTargets()[0];
  REQUIRE(orchestrator.StopCollection(target_ptr.get()) == ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
}

TEST_CASE("Orchestrator::ReadImmediate delegates to CollectorManager", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, ReadImmediateOnTarget(_)).RETURN(ASTL_STATUS_INTERNAL_ERROR);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  TestTargetBase target{"immediate-target"};
  REQUIRE(orchestrator.ReadImmediate(&target) == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("Orchestrator::GetProcessedMetricSamples validates inputs and uses fast path", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                                        target_uptr = std::make_unique<TestTargetBase>("processed-target");
  auto*                                       target      = target_uptr.get();
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  TestMetricBase                                metric{"processed-metric"};
  const std::vector<astl::ProcessedSampledData> processed_samples{
      {astl::AstlValue{uint64_t{9}}, astl::SampleTimestamp{}}
  };

  REQUIRE_FALSE(orchestrator.GetProcessedMetricSamples(nullptr, target).has_value());
  REQUIRE_FALSE(orchestrator.GetProcessedMetricSamples(&metric, nullptr).has_value());

  REQUIRE(orchestrator.SinkProcessedSamples(target, &metric, processed_samples) == ASTL_STATUS_SUCCESS);

  auto samples_or_err = orchestrator.GetProcessedMetricSamples(&metric, target);
  REQUIRE(samples_or_err.has_value());
  REQUIRE(samples_or_err->size() == 1);
  REQUIRE(samples_or_err->front().value == astl::AstlValue{uint64_t{9}});
}

TEST_CASE("Orchestrator::GetProcessedMetricSamples returns empty when cache is absent", "[Orchestrator]") {
  const auto      cache_dir = std::filesystem::temp_directory_path() / "astl_missing_processed_cache";
  std::error_code remove_error_code;
  std::filesystem::remove_all(cache_dir, remove_error_code);

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), cache_dir);

  auto                                        target_uptr = std::make_unique<TestTargetBase>("missing-cache-target");
  auto*                                       target      = target_uptr.get();
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  TestMetricBase metric{"missing-cache-metric"};
  auto           samples_or_err = orchestrator.GetProcessedMetricSamples(&metric, target);
  REQUIRE(samples_or_err.has_value());
  REQUIRE(samples_or_err->empty());
}

TEST_CASE("Orchestrator::LoadFromFile short-circuits when instance already exists", "[Orchestrator][cache]") {
  namespace fs = std::filesystem;

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto output_manager = std::make_unique<MockOutputManager>();

  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                           std::move(metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  const fs::path bogus_file = fs::temp_directory_path() / "already_initialized.astl";
  const fs::path cache_dir  = fs::temp_directory_path() / "already_initialized_cache";

  REQUIRE(astl::Orchestrator::LoadFromFile(bogus_file, cache_dir) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-SinkRawSamples empty span no-op", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_handle; _1->name = "mock_target")
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  std::vector<astl::RawSampledData> none;
  REQUIRE(orchestrator.SinkRawSamples(target, none) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-SinkRawSamples bulk growth then skip reserve", "[Orchestrator]") {
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT(_1->handle = mock_target_handle; _1->name = "mock_target")
      .RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  mock_targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  // First insertion triggers growth branch
  std::vector<astl::RawSampledData> batch1;
  batch1.emplace_back(static_cast<astl::OperationId>(0), astl::AstlValue{uint64_t{42}});
  REQUIRE(orchestrator.SinkRawSamples(target, batch1) == ASTL_STATUS_SUCCESS);

  // Second insertion fits existing capacity (skip reserve)
  std::vector<astl::RawSampledData> batch2;
  batch2.emplace_back(static_cast<astl::OperationId>(1), astl::AstlValue{uint64_t{43}});
  batch2.emplace_back(static_cast<astl::OperationId>(2), astl::AstlValue{uint64_t{44}});
  REQUIRE(orchestrator.SinkRawSamples(target, batch2) == ASTL_STATUS_SUCCESS);

  // Third insertion exceeds capacity -> growth reserve again
  std::vector<astl::RawSampledData> batch3;
  for (int i = 3; i < 9; ++i) {
    auto sample_value = static_cast<uint64_t>(100 + static_cast<uint64_t>(i));
    batch3.emplace_back(static_cast<astl::OperationId>(i), astl::AstlValue{sample_value});
  }
  REQUIRE(orchestrator.SinkRawSamples(target, batch3) == ASTL_STATUS_SUCCESS);
}

// Refactored: individual test cases for each emission scenario reduce cognitive complexity
TEST_CASE("Orchestrator-StopCollection INTERVAL_CSV only emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  auto          path = std::filesystem::temp_directory_path() / "orch_intervalcsv_only.csv";
  TempFileGuard interval_csv_guard(path);
  EnvVarGuard   perfetto_guard{astl::EnvVar::ASTL_OUTPUT_PERFETTO, ""};
  EnvVarGuard   interval_guard{astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, path.string()};

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::INTERVAL_CSV, _, _))
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string name = "mock_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-StopCollection PERFETTO only emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  auto          path = std::filesystem::temp_directory_path() / "orch_perfetto_only.json";
  TempFileGuard perfetto_file_guard(path);
  EnvVarGuard   interval_guard{astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, ""};
  EnvVarGuard   perfetto_guard{astl::EnvVar::ASTL_OUTPUT_PERFETTO, path.string()};

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);  // should be called after StopOnTarget

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::PERFETTO, _, _))
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string name = "mock_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-StopCollection dual PERFETTO+INTERVAL_CSV ordered emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  auto          perf_path = std::filesystem::temp_directory_path() / "orch_both_perfetto.json";
  auto          csv_path  = std::filesystem::temp_directory_path() / "orch_both_intervalcsv.csv";
  TempFileGuard perfetto_file_guard(perf_path);
  TempFileGuard interval_csv_file_guard(csv_path);
  EnvVarGuard   perfetto_guard{astl::EnvVar::ASTL_OUTPUT_PERFETTO, perf_path.string()};
  EnvVarGuard   interval_guard{astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, csv_path.string()};

  trompeloeil::sequence seq;  // enforce ordering

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::PERFETTO, _, _))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::INTERVAL_CSV, _, _))
      .IN_SEQUENCE(seq)
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string name = "mock_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-StopCollection INTERVAL_CSV idempotent emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  auto          csv_path = std::filesystem::temp_directory_path() / "orch_intervalcsv_idempotent.csv";
  TempFileGuard interval_csv_file_guard(csv_path);
  EnvVarGuard   perfetto_guard{astl::EnvVar::ASTL_OUTPUT_PERFETTO, ""};
  EnvVarGuard   interval_guard{astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, csv_path.string()};

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);  // second call
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::INTERVAL_CSV, _, _))
      .RETURN(ASTL_STATUS_SUCCESS);  // single emission

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string name = "mock_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-FullLifecyclePositive", "[Orchestrator][lifecycle]") {
  // Clear output env vars so that emission tests that ran before us don't pollute this test.
  (void)astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, "");
  (void)astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_PERFETTO, "");

  using State            = astl::Orchestrator::TargetCollectionState;
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "lifecycle_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  auto state0 = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state0);
  REQUIRE(state0.value() == State::UNCONFIGURED);

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);
  auto state1 = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state1);
  REQUIRE(state1.value() == State::CONFIGURED);

  // Unsupported metric path (bogus pointer) should not alter state
  static int                          unsupported_metric_storage = 0;
  std::array<astl_metric_handle_t, 1> bad_metrics{&unsupported_metric_storage};
  (void)orchestrator.ConfigureMetricCollection(target, &params, bad_metrics);
  auto state1b = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state1b);
  REQUIRE(state1b.value() == State::CONFIGURED);

  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_SUCCESS);
  auto state2 = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state2);
  REQUIRE(state2.value() == State::STARTED);
  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_COLLECTION_ALREADY_RUNNING);

  REQUIRE(orchestrator.PauseCollection(target) == ASTL_STATUS_SUCCESS);
  auto state3 = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state3);
  REQUIRE(state3.value() == State::PAUSED);
  REQUIRE(orchestrator.PauseCollection(target) == ASTL_STATUS_COLLECTION_ALREADY_PAUSED);
  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_INVALID_STATE_TRANSITION);

  REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_SUCCESS);
  auto state4 = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state4);
  REQUIRE(state4.value() == State::STARTED);
  REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_COLLECTION_ALREADY_RUNNING);

  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
  auto state5 = orchestrator.GetTargetCollectionState(target);
  REQUIRE(state5);
  REQUIRE(state5.value() == State::STOPPED);
  // Current implementation treats repeated Stop as idempotent success
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.PauseCollection(target) == ASTL_STATUS_COLLECTION_NOT_RUNNING);
  REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_COLLECTION_NOT_PAUSED);
  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_INVALID_STATE_TRANSITION);
}

TEST_CASE("Orchestrator-ConfigureMetricCollection resets per-target cached artifacts", "[Orchestrator][lifecycle]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_orchestrator_target_reset_test";
  TempFileGuard  cache_guard(cache_dir);

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  auto                              mock_metric     = std::make_unique<MockMetric>();
  auto*                             mock_metric_ptr = mock_metric.get();
  static const std::string          metric_name     = "cached_metric";

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });
  ALLOW_CALL(*mock_metric, Name()).RETURN(metric_name);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), cache_dir);

  auto                     mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t     mock_target_handle = mock_target.get();
  static const std::string target_name        = "cached_target";
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  const astl::ProcessedSampledData existing_sample{astl::AstlValue{uint64_t{7}}, astl::SampleTimestamp{}};
  REQUIRE(orchestrator.SinkProcessedSamples(target, mock_metric_ptr, {&existing_sample, 1}) == ASTL_STATUS_SUCCESS);

  fs::create_directories(cache_dir);
  const fs::path cached_sample_file = cache_dir / (target_name + astl::kAstlFileExtension);
  std::ofstream  cached_file(cached_sample_file, std::ios::binary);
  REQUIRE(cached_file.good());
  cached_file << "stale";
  cached_file.close();
  REQUIRE(fs::exists(cached_sample_file));

  auto before_reset = orchestrator.GetProcessedSamplesSnapshot();
  REQUIRE(before_reset.contains(target));
  REQUIRE(before_reset.at(target).contains(mock_metric_ptr));

  astl_collection_params_t params{};
  params.size            = sizeof(params);
  params.collection_mode = ASTL_COLLECTION_MODE_SNAPSHOT;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);
  REQUIRE_FALSE(fs::exists(cached_sample_file));

  auto after_reset = orchestrator.GetProcessedSamplesSnapshot();
  REQUIRE_FALSE(after_reset.contains(target));
}

TEST_CASE("Orchestrator-StartCollectionPaused transitions directly to paused", "[Orchestrator][lifecycle]") {
  using State            = astl::Orchestrator::TargetCollectionState;
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "paused_start_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);

  REQUIRE(orchestrator.StartCollectionPaused(target) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::PAUSED);
  REQUIRE(orchestrator.ResumeCollection(target) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::STARTED);
}

TEST_CASE("Orchestrator-StartCollectionPaused rolls back when pause is unsupported", "[Orchestrator][lifecycle]") {
  using State            = astl::Orchestrator::TargetCollectionState;
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, PauseOnTarget(_)).RETURN(ASTL_STATUS_NOT_IMPLEMENTED);
  ALLOW_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "paused_start_unsupported_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);

  REQUIRE(orchestrator.StartCollectionPaused(target) == ASTL_STATUS_PAUSE_UNSUPPORTED);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);
}

TEST_CASE("Orchestrator-StartFailureDoesNotStickStartedState", "[Orchestrator][lifecycle]") {
  using State            = astl::Orchestrator::TargetCollectionState;
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, StartOnTarget(_)).RETURN(ASTL_STATUS_INTERNAL_ERROR);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "start_failure_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);

  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);

  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);
}

TEST_CASE("Orchestrator-StartSetsStartingStateDuringCollectorStart", "[Orchestrator][lifecycle]") {
  using State            = astl::Orchestrator::TargetCollectionState;
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};

  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });

  auto output_manager = std::make_unique<MockOutputManager>();

  auto starting_state_observer_orchestrator       = std::make_shared<astl::Orchestrator*>(nullptr);
  auto observed_starting_state_in_start_on_target = std::make_shared<bool>(false);
  ALLOW_CALL(*collector_manager, StartOnTarget(_))
      .SIDE_EFFECT({
        auto state_or_error = (*starting_state_observer_orchestrator)->GetTargetCollectionState(_1);
        *observed_starting_state_in_start_on_target =
            state_or_error.has_value() && state_or_error.value() == State::STARTING;
      })
      .RETURN(ASTL_STATUS_INTERNAL_ERROR);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  *starting_state_observer_orchestrator = &orchestrator;

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "start_in_progress_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);

  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(*observed_starting_state_in_start_on_target);
  *starting_state_observer_orchestrator = nullptr;
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);
}

TEST_CASE("Orchestrator-StopDuringStartingReturnsInvalidStateTransition", "[Orchestrator][lifecycle]") {
  // Verify that a StopCollection call that arrives while StartOnTarget() is in flight (state==STARTING)
  // is rejected with INVALID_STATE_TRANSITION, and that the final state after the start completes
  // is correctly STARTED (i.e., the guard in StartCollection does not observe the transient stop).
  using State            = astl::Orchestrator::TargetCollectionState;
  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, IsAnyTargetBeingCollected()).RETURN(false);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });

  auto output_manager = std::make_unique<MockOutputManager>();

  auto stop_during_start_result             = std::make_shared<astl_status_code>(ASTL_STATUS_SUCCESS);
  auto starting_state_observer_orchestrator = std::make_shared<astl::Orchestrator*>(nullptr);
  // During StartOnTarget, simulate a concurrent StopCollection arriving while state is STARTING.
  ALLOW_CALL(*collector_manager, StartOnTarget(_))
      .SIDE_EFFECT({ *stop_during_start_result = (*starting_state_observer_orchestrator)->StopCollection(_1); })
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");
  *starting_state_observer_orchestrator = &orchestrator;

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "stop_during_starting_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};
  REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::CONFIGURED);

  // StartCollection succeeds; the simulated concurrent stop is rejected.
  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_SUCCESS);
  *starting_state_observer_orchestrator = nullptr;

  // The concurrent StopCollection must have been rejected while state was STARTING.
  REQUIRE(*stop_during_start_result == ASTL_STATUS_INVALID_STATE_TRANSITION);
  // StartCollection correctly set the final state to STARTED.
  REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::STARTED);
}

TEST_CASE("Orchestrator::ConfigureMetricCollection preserves state on operation-build and collector failures",
          "[Orchestrator][lifecycle]") {
  using State                 = astl::Orchestrator::TargetCollectionState;
  auto  topology_manager      = std::make_unique<MockTopologyManager>();
  auto  collector_manager     = std::make_unique<MockCollectorManager>();
  auto* collector_manager_raw = collector_manager.get();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto                              metric_manager       = std::make_unique<MockMetricManager>();
  auto*                             metric_manager_raw   = metric_manager.get();
  static int                        dummy_metric_storage = 0;
  astl_metric_handle_t              metric_handle        = &dummy_metric_storage;
  std::vector<astl_metric_handle_t> available_metrics{metric_handle};
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_))
      .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics});

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "configure_metric_failure_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_metric_handle_t, 1> metrics{metric_handle};

  SECTION("GetRequiredOperations failure maps to INTERNAL_ERROR") {
    REQUIRE_CALL(*metric_manager_raw, GetRequiredOperations(_, _)).RETURN(std::unexpected(ASTL_STATUS_BAD_ARGUMENT));

    REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_INTERNAL_ERROR);
    REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::UNCONFIGURED);
  }

  SECTION("collector configuration failure is propagated") {
    REQUIRE_CALL(*metric_manager_raw, GetRequiredOperations(_, _))
        .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
            astl::CollectionOperations{
                                       {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
    });
    REQUIRE_CALL(*collector_manager_raw, ConfigureCollectionOnTarget(_, _, _))
        .RETURN(ASTL_STATUS_COLLECTION_ALREADY_RUNNING);

    REQUIRE(orchestrator.ConfigureMetricCollection(target, &params, metrics) == ASTL_STATUS_COLLECTION_ALREADY_RUNNING);
    REQUIRE(orchestrator.GetTargetCollectionState(target).value() == State::UNCONFIGURED);
  }
}

TEST_CASE("Orchestrator::ConfigureCounterCollection propagates collector errors", "[Orchestrator][lifecycle]") {
  auto  topology_manager      = std::make_unique<MockTopologyManager>();
  auto  collector_manager     = std::make_unique<MockCollectorManager>();
  auto* collector_manager_raw = collector_manager.get();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto                               metric_manager        = std::make_unique<MockMetricManager>();
  static int                         dummy_counter_storage = 0;
  astl_counter_handle_t              counter_handle        = &dummy_counter_storage;
  std::vector<astl_counter_handle_t> available_counters{counter_handle};
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetAvailableCounters(_))
      .RETURN(std::expected<std::span<const astl_counter_handle_t>, astl_status_code>{available_counters});
  ALLOW_CALL(*metric_manager, GetCounterRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });
  REQUIRE_CALL(*collector_manager_raw, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_BAD_ARGUMENT);

  auto               output_manager = std::make_unique<MockOutputManager>();
  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager), "");

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  static const std::string target_name = "configure_counter_failure_target";
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(orchestrator.SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* target = orchestrator.GetTargets()[0].get();

  astl_collection_params_t params{};
  params.size  = sizeof(params);
  params.flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY;
  std::array<astl_counter_handle_t, 1> counters{counter_handle};

  REQUIRE(orchestrator.ConfigureCounterCollection(target, &params, counters) == ASTL_STATUS_BAD_ARGUMENT);
}

/******************************************************************************
 *  SaveToFile / SaveStateToCacheDir / LoadFromFile tests                     *
 ******************************************************************************/

auto MakeMinimalOrchestratorForSave(std::filesystem::path cache_dir)
    -> std::pair<std::unique_ptr<astl::Orchestrator>, std::vector<std::unique_ptr<trompeloeil::expectation>>> {
  auto                                                   topology_manager  = std::make_unique<MockTopologyManager>();
  auto                                                   collector_manager = std::make_unique<MockCollectorManager>();
  std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  auto metric_manager = std::make_unique<MockMetricManager>();
  expectations.push_back(
      NAMED_ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*metric_manager, RemoveAllMetrics()));
  auto output_manager = std::make_unique<MockOutputManager>();
  return {std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                               std::move(metric_manager), std::move(output_manager), cache_dir),
          std::move(expectations)};
}

TEST_CASE("Orchestrator::SaveToFile round-trip", "[Orchestrator][cache]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_orch_save_to_file_test";
  const fs::path save_file = fs::temp_directory_path() / "astl_orch_save_to_file_test.astl";
  TempFileGuard  cache_guard(cache_dir);
  TempFileGuard  save_guard(save_file);

  // Build a concrete orchestrator with a real TopologyManager so serialization succeeds.
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  auto metric_manager = std::make_unique<MockMetricManager>();
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto output_manager = std::make_unique<MockOutputManager>();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));
  auto topology_manager = std::make_unique<astl::TopologyManager>(std::move(targets));

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager), cache_dir);
  TestOrchestratorInjector injector(std::move(orchestrator));

  // SaveToFile exercises SaveStateToCacheDir (L607+) then ZipDirectory (L604)
  // MockMetricManager makes Serialize fail with BAD_ARGUMENT on the metric manager path,
  // but topology serialization succeeds (concrete TopologyManager). The overall status
  // from SaveStateToCacheDir returns that failure.
  auto status = astl::Orchestrator::SaveToFile(save_file);
  // With a mock metric manager serialization will fail.
  REQUIRE(status == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("Orchestrator::SaveStateToCacheDir serialises topology + metric manager", "[Orchestrator][cache]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_state_cache_test";
  TempFileGuard  cache_guard(cache_dir);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  // Use a concrete MetricManager so serialization actually succeeds end-to-end.
  std::vector<astl::CollectorCapability> collector_caps;
  collector_caps.emplace_back(astl::CollectorType::SCMI);
  std::vector<astl::SystemCapability> system_caps;
  system_caps.emplace_back();
  astl::Capabilities caps{std::move(collector_caps), std::move(system_caps)};
  auto               concrete_metric_manager = std::make_unique<astl::MetricManager>(caps);

  auto output_manager = std::make_unique<MockOutputManager>();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));
  auto topology_manager = std::make_unique<astl::TopologyManager>(std::move(targets));

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(concrete_metric_manager), std::move(output_manager), cache_dir);
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astl::Orchestrator::SaveStateToCacheDir() == ASTL_STATUS_SUCCESS);

  // Verify topology and metric manager files were created in the cache dir.
  REQUIRE(fs::exists(cache_dir / astl::kTopologyManagerFileName));
  REQUIRE(fs::exists(cache_dir / astl::kMetricManagerFileName));
}

TEST_CASE("Orchestrator::LoadFromFile fails for non-existent file", "[Orchestrator][cache]") {
  namespace fs = std::filesystem;

  const fs::path bad_file  = "/tmp/astl_nonexistent_load_test_12345.astl";
  const fs::path cache_dir = fs::temp_directory_path() / "astl_load_fail_cache";
  TempFileGuard  cache_guard(cache_dir);

  // Ensure singleton is clear so we don't short-circuit with the "already initialized" path.
  astl::Orchestrator::ResetInstance();
  auto status = astl::Orchestrator::LoadFromFile(bad_file, cache_dir);
  REQUIRE(status != ASTL_STATUS_SUCCESS);

  // Restore a minimal orchestrator to leave the singleton in a valid state.
  auto [orchestrator, expectations] = MakeMinimalOrchestratorForSave("");
  TestOrchestratorInjector injector(std::move(orchestrator));
}

TEST_CASE("Orchestrator::SaveToFile creates a valid .astl archive", "[Orchestrator][cache]") {
  namespace fs = std::filesystem;

  const fs::path cache_dir = fs::temp_directory_path() / "astl_save_file_roundtrip_cache";
  const fs::path save_file = fs::temp_directory_path() / "astl_save_file_roundtrip.astl";
  const fs::path load_dir  = fs::temp_directory_path() / "astl_save_file_roundtrip_load";
  TempFileGuard  cache_guard(cache_dir);
  TempFileGuard  save_guard(save_file);
  TempFileGuard  load_guard(load_dir);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  std::vector<astl::CollectorCapability> collector_caps;
  collector_caps.emplace_back(astl::CollectorType::SCMI);
  std::vector<astl::SystemCapability> system_caps;
  system_caps.emplace_back();
  astl::Capabilities caps{std::move(collector_caps), std::move(system_caps)};
  auto               concrete_metric_manager = std::make_unique<astl::MetricManager>(caps);

  auto output_manager = std::make_unique<MockOutputManager>();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));
  auto topology_manager = std::make_unique<astl::TopologyManager>(std::move(targets));

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(concrete_metric_manager), std::move(output_manager), cache_dir);
  TestOrchestratorInjector injector(std::move(orchestrator));

  REQUIRE(astl::Orchestrator::SaveToFile(save_file) == ASTL_STATUS_SUCCESS);

  // Verify the archive is a valid zip containing the expected files.
  REQUIRE(fs::exists(save_file));
  REQUIRE(astl::mz::UnzipDirectory(save_file, load_dir) == ASTL_STATUS_SUCCESS);
  REQUIRE(fs::exists(load_dir / astl::kTopologyManagerFileName));
  REQUIRE(fs::exists(load_dir / astl::kMetricManagerFileName));
}
