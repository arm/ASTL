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
  REQUIRE(orchestrator.SetTargets(std::move(mock_targets)) == ASTL_STATUS_SUCCESS);

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

TEST_CASE("Orchestrator-SinkRawSamples empty span no-op", "[Orchestrator]") {
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
  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = astl::Orchestrator(std::move(topology_manager), std::move(collector_manager),
                                           std::move(metric_manager), std::move(output_manager));

  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
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
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");      // clear PERFETTO
  (void)astl::SetEnvVar("ASTL_OUTPUT_INTERVAL_CSV", "");  // ensure clean slate before setting
  auto path = std::filesystem::temp_directory_path() / "orch_intervalcsv_only.csv";
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_INTERVAL_CSV", path.string()) == ASTL_STATUS_SUCCESS);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::INTERVAL_CSV, _, _))
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-StopCollection PERFETTO only emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  (void)astl::SetEnvVar("ASTL_OUTPUT_INTERTVAL_CSV", "");  // clear INTERVAL_CSV
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");       // ensure clean slate before setting
  auto path = std::filesystem::temp_directory_path() / "orch_perfetto_only.json";
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", path.string()) == ASTL_STATUS_SUCCESS);

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::PERFETTO, _, _))
      .RETURN(ASTL_STATUS_SUCCESS);

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-StopCollection dual PERFETTO+INTERVAL_CSV ordered emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");
  (void)astl::SetEnvVar("ASTL_OUTPUT_INTERVAL_CSV", "");  // clear both first
  auto perf_path = std::filesystem::temp_directory_path() / "orch_both_perfetto.json";
  auto csv_path  = std::filesystem::temp_directory_path() / "orch_both_intervalcsv.csv";
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", perf_path.string()) == ASTL_STATUS_SUCCESS);
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_INTERVAL_CSV", csv_path.string()) == ASTL_STATUS_SUCCESS);

  trompeloeil::sequence seq;  // enforce ordering

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

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
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Orchestrator-StopCollection INTERVAL_CSV idempotent emission", "[Orchestrator][outputs]") {
  using trompeloeil::_;
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");
  (void)astl::SetEnvVar("ASTL_OUTPUT_INTERVAL_CSV", "");  // clear both first
  auto csv_path = std::filesystem::temp_directory_path() / "orch_intervalcsv_idempotent.csv";
  REQUIRE(astl::SetEnvVar("ASTL_OUTPUT_INTERVAL_CSV", csv_path.string()) == ASTL_STATUS_SUCCESS);
  (void)astl::SetEnvVar("ASTL_OUTPUT_PERFETTO", "");  // clear PERFETTO

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*collector_manager, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);  // second call

  auto metric_manager = std::make_unique<MockMetricManager>();
  REQUIRE_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*metric_manager, ProcessRawSamples(_)).RETURN(ASTL_STATUS_SUCCESS);  // second attempt
  REQUIRE_CALL(*metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS);    // second attempt
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  REQUIRE_CALL(*output_manager, OutputProcessedSamples(_, astl::OutputType::INTERVAL_CSV, _, _))
      .RETURN(ASTL_STATUS_SUCCESS);  // single emission

  auto                 topology_manager   = std::make_unique<MockTopologyManager>();
  auto                 mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));
  REQUIRE(topology_manager->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  astl::Orchestrator orchestrator(std::move(topology_manager), std::move(collector_manager), std::move(metric_manager),
                                  std::move(output_manager));
  auto*              target = orchestrator.GetTargets()[0].get();
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator.StopCollection(target) == ASTL_STATUS_SUCCESS);
}
