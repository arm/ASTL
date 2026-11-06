// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <functional>
#include <future>

#include "../../mock_classes.hpp"
#include "../../orchestrator_lifecycle_test_utils.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "common/clock_correlation.hpp"
#include "metric/metric_manager.hpp"
#include "scmi/scmi_operation_builder.hpp"

using trompeloeil::_;

namespace {
class ClockTestTarget : public TestTargetBase {
 public:
  using TestTargetBase::TestTargetBase;
  auto GetProperties(astl_target_props_t* props) const -> astl_status_code override {
    const auto status = TestTargetBase::GetProperties(props);
    if (status == ASTL_STATUS_SUCCESS) {
      props->name = Name().c_str();
    }
    return status;
  }
};

struct ClockConfigurationFixture {
  using State = astl::Orchestrator::TargetCollectionState;
  EnvVarGuard   perfetto{astl::EnvVar::ASTL_OUTPUT_PERFETTO, ""};
  EnvVarGuard   interval_csv{astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, ""};
  EnvVarGuard   summary_csv{astl::EnvVar::ASTL_OUTPUT_SUMMARY_CSV, ""};
  TempFileGuard cache{
      std::filesystem::temp_directory_path() /
      ("astl-config-clock-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
  std::vector<std::unique_ptr<trompeloeil::expectation>>      expectations;
  MockCollectorManager*                                       collectors{};
  astl::MetricManager*                                        metrics{};
  astl::IRawSampleSink*                                       sink{};
  const astl::ITarget*                                        target0{};
  const astl::ITarget*                                        target1{};
  const astl::ITarget*                                        fail_snapshot{};
  astl_status_code                                            cleanup_status{ASTL_STATUS_SUCCESS};
  std::unordered_map<const astl::ITarget*, astl::OperationId> operations;
  std::unordered_map<const astl::ITarget*, int>               captures;
  std::vector<const astl::ITarget*>                           cleared;
  uint64_t                                                    sample_tick{762698287415780};
  uint64_t                                                    native_anchor{762698277644768};
  int64_t                                                     host_anchor{762243025495040};
  std::function<void()>                                       before_snapshot;
  std::unique_ptr<astl::Orchestrator>                         orchestrator;

  ClockConfigurationFixture() {
    auto collector_owner = std::make_unique<MockCollectorManager>();
    collectors           = collector_owner.get();
    expectations.push_back(
        NAMED_ALLOW_CALL(*collectors, RegisterRawSampleSink(_)).LR_SIDE_EFFECT(sink = _1).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, ConfigureCollectionOnTarget(_, _, _))
                               .LR_SIDE_EFFECT(operations[_1] = _3.operationsOnSample.front()->GetId())
                               .RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, GetNativeClockSnapshot(_)).LR_RETURN(Snapshot(_1)));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, ClearConfiguredCollectionOnTarget(_))
                               .LR_SIDE_EFFECT(cleared.push_back(_1); operations.erase(_1))
                               .LR_RETURN(cleanup_status));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, ReadImmediateOnTarget(_)).LR_RETURN(Emit(_1)));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, PauseOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, ResumeOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*collectors, IsAnyTargetBeingCollected()).RETURN(false));
    auto metric_owner = std::make_unique<astl::MetricManager>(
        astl::Capabilities{{astl::CollectorCapability{astl::CollectorType::SCMI}}, {astl::SystemCapability{}}});
    metrics      = metric_owner.get();
    orchestrator = std::make_unique<astl::Orchestrator>(std::make_unique<MockTopologyManager>(),
                                                        std::move(collector_owner), std::move(metric_owner),
                                                        std::make_unique<MockOutputManager>(), cache.path);
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(std::make_unique<ClockTestTarget>("clock-target-0"));
    target0 = targets.back().get();
    targets.push_back(std::make_unique<ClockTestTarget>("clock-target-1"));
    target1 = targets.back().get();
    REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
    REQUIRE(metrics->RegisterMetric(Config("clock-value"), {target0, target1}) == ASTL_STATUS_SUCCESS);
    REQUIRE(metrics->RegisterCounter(Config("clock-counter"), {target0, target1}) == ASTL_STATUS_SUCCESS);
  }

  static auto Config(const std::string& name) -> std::unique_ptr<astl::MetricConfig> {
    return std::make_unique<astl::MetricConfig>(name, "clock regression", ASTL_UNITS_NONE, ASTL_VALUE_UINT64,
                                                ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
                                                astl::CollectorType::SCMI, astl::ScmiOperationBuilder{0x9b05});
  }

  auto Configure(const astl::ITarget* target, bool counter = false) const -> astl_status_code {
    astl_collection_params_t params{};
    params.size            = sizeof(params);
    params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;
    params.flags           = ASTL_NO_CACHING;
    if (counter) {
      return orchestrator->ConfigureCounterCollection(target, &params, *metrics->GetAvailableCounters(target));
    }
    // Pause/resume may register a lifecycle metric; only select the original data metric.
    return orchestrator->ConfigureMetricCollection(target, &params, metrics->GetAvailableMetrics(target)->first(1));
  }

  auto Snapshot(const astl::ITarget* target) -> std::expected<astl::ClockCorrelationMap, astl_status_code> {
    ++captures[target];
    if (before_snapshot) {
      before_snapshot();
    }
    if (target == fail_snapshot) {
      return std::unexpected{ASTL_STATUS_FILE_ERROR};
    }
    return astl::ClockCorrelationMap{
        {operations.at(target),
         astl::OperationClockCorrelation{
             astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{host_anchor}}, native_anchor, {1, 1}}}
    };
  }

  auto Emit(const astl::ITarget* target) -> astl_status_code {
    astl::RawSampledData sample{operations.at(target), astl::AstlValue{uint64_t{42}}, sample_tick};
    return sink->SinkRawSamples(target, {&sample, 1});
  }

  auto Samples(const astl::ITarget* target) const -> std::vector<astl::ProcessedSampledData> {
    const auto all   = orchestrator->GetProcessedSamplesSnapshot();
    const auto found = all.find(target);
    if (found == all.end() || found->second.empty()) {
      return {};
    }
    return found->second.begin()->second;
  }
};
}  // namespace

TEST_CASE_METHOD(ClockConfigurationFixture, "Configuration keeps correlation stable across immediate reads",
                 "[Orchestrator][ClockCorrelation]") {
  const bool counter = GENERATE(false, true);
  REQUIRE(Configure(target0, counter) == ASTL_STATUS_SUCCESS);
  REQUIRE(captures[target0] == 1);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);

  // Recorded ASTL-694 case: refreshing this anchor would move the second sample backwards by 375320 ns.
  host_anchor   = 762243034890732;
  native_anchor = 762698288428868;
  sample_tick   = native_anchor;
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  const auto samples = Samples(target0);
  REQUIRE(samples.size() == 2);
  REQUIRE(samples[0].timestamp.time_since_epoch().count() == 762243035266052);
  REQUIRE(samples[1].timestamp.time_since_epoch().count() == 762243036279140);
  REQUIRE(captures[target0] == 1);

  // A real regression is still rejected; the fix does not clamp or hide backward hardware timestamps.
  --sample_tick;
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(Samples(target0).size() == 2);
}

TEST_CASE_METHOD(ClockConfigurationFixture, "Start and pause resume retain configuration correlation",
                 "[Orchestrator][ClockCorrelation]") {
  REQUIRE(Configure(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  const auto operation = operations.at(target0);
  // Simulate a later clock reading without depending on wall-clock delays in the test.
  host_anchor += 1000000000;
  native_anchor += 1000000000;
  REQUIRE(orchestrator->StartCollection(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->PauseCollection(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ResumeCollection(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(captures[target0] == 1);
  REQUIRE(metrics->GetClockCorrelations().at(operation).native_at_start == 762698277644768);
  REQUIRE(orchestrator->StopCollection(target0) == ASTL_STATUS_SUCCESS);
}

TEST_CASE_METHOD(ClockConfigurationFixture, "Reconfiguration replaces only that target's correlation and history",
                 "[Orchestrator][ClockCorrelation]") {
  const bool counter = GENERATE(false, true);
  REQUIRE(Configure(target0, counter) == ASTL_STATUS_SUCCESS);
  REQUIRE(Configure(target1) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target1) == ASTL_STATUS_SUCCESS);
  const auto old_operation   = operations.at(target0);
  const auto other_operation = operations.at(target1);
  host_anchor                = 1000;
  native_anchor              = 100;
  sample_tick                = 101;
  REQUIRE(Configure(target0, counter) == ASTL_STATUS_SUCCESS);
  REQUIRE_FALSE(metrics->GetClockCorrelations().contains(old_operation));
  REQUIRE(metrics->GetClockCorrelations().contains(other_operation));
  REQUIRE(Samples(target0).empty());
  REQUIRE(Samples(target1).size() == 1);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(Samples(target0).front().timestamp.time_since_epoch().count() == 1001);
  REQUIRE(captures[target0] == 2);
  REQUIRE(captures[target1] == 1);
}

TEST_CASE_METHOD(ClockConfigurationFixture, "Failed configuration clears the target and permits a fresh retry",
                 "[Orchestrator][ClockCorrelation]") {
  const bool counter = GENERATE(false, true);
  REQUIRE(Configure(target0, counter) == ASTL_STATUS_SUCCESS);
  REQUIRE(Configure(target1) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target1) == ASTL_STATUS_SUCCESS);
  const auto other_operation = operations.at(target1);
  fail_snapshot              = target0;
  SECTION("Collector rollback succeeds") {}
  SECTION("Collector rollback fails but acquisition stays disabled") { cleanup_status = ASTL_STATUS_INTERNAL_ERROR; }
  REQUIRE(Configure(target0, counter) == ASTL_STATUS_FILE_ERROR);
  REQUIRE(cleared == std::vector<const astl::ITarget*>{target0});
  REQUIRE(orchestrator->GetTargetCollectionState(target0).value() == State::UNCONFIGURED);
  REQUIRE(orchestrator->StartCollection(target0) == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(metrics->GetClockCorrelations().size() == 1);
  REQUIRE(metrics->GetClockCorrelations().contains(other_operation));
  REQUIRE(Samples(target0).empty());
  REQUIRE(Samples(target1).size() == 1);
  REQUIRE(orchestrator->GetTargetCollectionState(target1).value() == State::CONFIGURED);

  fail_snapshot = nullptr;
  REQUIRE(Configure(target0, counter) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator->ReadImmediate(target0) == ASTL_STATUS_SUCCESS);
  REQUIRE(Samples(target0).size() == 1);
  REQUIRE(captures[target0] == 3);
}

TEST_CASE_METHOD(ClockConfigurationFixture, "Acquisition waits for configuration to publish its correlation",
                 "[Orchestrator][ClockCorrelation][thread-safety]") {
  const bool         start = GENERATE(false, true);
  std::promise<void> entered;
  std::promise<void> release;
  auto               released = release.get_future().share();
  before_snapshot             = [&] {
    entered.set_value();
    released.wait();
  };
  auto       configuring    = std::async(std::launch::async, [&] { return Configure(target0); });
  const auto entered_status = entered.get_future().wait_for(std::chrono::seconds{5});
  if (entered_status != std::future_status::ready) {
    release.set_value();
    REQUIRE(entered_status == std::future_status::ready);
  }
  std::promise<void> acquiring;
  auto               acquisition = std::async(std::launch::async, [&] {
    acquiring.set_value();
    return start ? orchestrator->StartCollection(target0) : orchestrator->ReadImmediate(target0);
  });
  acquiring.get_future().wait();
  const auto waiting = acquisition.wait_for(std::chrono::milliseconds{50});
  release.set_value();
  REQUIRE(configuring.get() == ASTL_STATUS_SUCCESS);
  REQUIRE(acquisition.get() == ASTL_STATUS_SUCCESS);
  REQUIRE(waiting == std::future_status::timeout);
  REQUIRE(captures[target0] == 1);
  if (start) {
    REQUIRE(orchestrator->StopCollection(target0) == ASTL_STATUS_SUCCESS);
  } else {
    REQUIRE(Samples(target0).size() == 1);
  }
}
