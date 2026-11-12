// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <memory>

#include "collector/collector_manager.hpp"
#include "scmi/scmi_ioctl_collector.hpp"
#include "scmi/scmi_operation_builder.hpp"
#include "scmi/scmi_target.hpp"
#include "wrapper_utils.hpp"

namespace {
using trompeloeil::_;

struct MockScmiIoctlInterface : astl::IScmiIoctlInterface {
  using ExpectedString = std::expected<std::string, astl_status_code>;
  using ExpectedCount  = std::expected<uint32_t, astl_status_code>;
  using ExpectedBool   = std::expected<bool, astl_status_code>;

  MAKE_CONST_MOCK0(DevicePath, const std::filesystem::path&(), override);
  MAKE_MOCK0(Probe, astl_status_code(), override);
  MAKE_MOCK0(DeImplementationVersion, ExpectedString(), override);
  MAKE_MOCK0(DataEventCount, ExpectedCount(), override);
  MAKE_MOCK0(SupportsSingleRead, ExpectedBool(), override);
  MAKE_MOCK1(GetConfig, astl_status_code(scmi_tlm_config&), override);
  MAKE_MOCK1(SetConfig, astl_status_code(scmi_tlm_config&), override);
  MAKE_MOCK2(GetDataEventConfig, astl_status_code(astl::ScmiDataEventId, scmi_tlm_de_config&), override);
  MAKE_MOCK1(SetDataEventConfig, astl_status_code(scmi_tlm_de_config&), override);
  MAKE_MOCK2(GetDataEventInfo, astl_status_code(astl::ScmiDataEventId, scmi_tlm_de_info&), override);
  MAKE_MOCK2(ReadDataEventValue, astl_status_code(astl::ScmiDataEventId, scmi_tlm_de_sample&), override);
  MAKE_MOCK2(ReadSingle, astl_status_code(std::span<scmi_tlm_de_sample>, uint32_t&), override);
  MAKE_MOCK0(Reset, astl_status_code(), override);
};
struct EmptyResultFixture {
  astl::ITarget*                                         target{};
  astl::MetricManager*                                   manager{};
  astl::ScmiIoctlCollector*                              collector{};
  MockScmiIoctlInterface*                                device{};
  astl::Orchestrator*                                    orchestrator{};
  astl_metric_handle_t                                   metric{};
  bool                                                   single_read{};
  std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;
  std::unique_ptr<TestOrchestratorInjector>              injector;

  explicit EmptyResultFixture(astl_metric_type_t type = ASTL_METRIC_VALUE, bool uncached = true) {
    auto                                        topology = std::make_unique<MockTopologyManager>();
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(std::make_unique<astl::ScmiTarget>("empty-result-test", "mock SCMI", "tlm-0", nullptr));
    target = targets.front().get();
    REQUIRE(topology->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
    auto interface = std::make_unique<MockScmiIoctlInterface>();
    device         = interface.get();
    expectations.push_back(NAMED_ALLOW_CALL(*device, Probe()).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*device, DeImplementationVersion()).RETURN(std::string(32, '0')));
    // Calibration requires a nonzero baseline; per-read expectations override this default.
    expectations.push_back(NAMED_ALLOW_CALL(*device, ReadDataEventValue(1, _))
                               .SIDE_EFFECT(_2 = {})
                               .SIDE_EFFECT(_2.tstamp = 1)
                               .RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*device, GetConfig(_))
                               .SIDE_EFFECT(_1 = {})
                               .SIDE_EFFECT(_1.active.secs = 1)
                               .SIDE_EFFECT(_1.active.exp = -9)
                               .RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*device, SetConfig(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(
        NAMED_ALLOW_CALL(*device, GetDataEventConfig(1, _)).SIDE_EFFECT(_2 = {}).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*device, SetDataEventConfig(_)).RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*device, GetDataEventInfo(1, _))
                               .SIDE_EFFECT(_2 = {})
                               .SIDE_EFFECT(_2.ts_rate = 1000)
                               .RETURN(ASTL_STATUS_SUCCESS));
    expectations.push_back(NAMED_ALLOW_CALL(*device, SupportsSingleRead()).LR_RETURN(single_read));
    expectations.push_back(NAMED_ALLOW_CALL(*device, DataEventCount()).RETURN(1U));
    auto scmi = std::make_unique<astl::ScmiIoctlCollector>(std::move(interface));
    collector = scmi.get();
    std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors;
    collectors[target].push_back(std::move(scmi));
    auto               collector_manager = std::make_unique<astl::CollectorManager>(std::move(collectors));
    astl::Capabilities caps{{astl::CollectorCapability{astl::CollectorType::SCMI}}, {astl::SystemCapability{}}};
    auto               metric_manager = std::make_unique<astl::MetricManager>(caps);
    manager                           = metric_manager.get();
    auto config = std::make_unique<astl::MetricConfig>("test.value", "test metric", ASTL_UNITS_COUNT, ASTL_VALUE_UINT64,
                                                       ASTL_METRIC_IDENTIFIER_UNKNOWN, type, astl::CollectorType::SCMI,
                                                       astl::ScmiOperationBuilder{1});
    // The target's owning vector was moved to topology, which is moved to the injected orchestrator below.
    // cppcheck-suppress danglingLifetime
    REQUIRE(manager->RegisterMetric(std::move(config), {target}) == ASTL_STATUS_SUCCESS);
    const auto metrics = manager->GetAvailableMetrics(target);
    REQUIRE(metrics.has_value());
    metric = metrics->front();
    auto owned =
        std::make_unique<astl::Orchestrator>(std::move(topology), std::move(collector_manager),
                                             std::move(metric_manager), std::make_unique<astl::OutputManager>(), "");
    orchestrator = owned.get();
    injector     = std::make_unique<TestOrchestratorInjector>(std::move(owned));
    Configure(uncached);
  }

  void Configure(bool uncached = true) {
    ASTL_INIT_STRUCT(astl_collection_params_t, config, .flags = uncached ? static_cast<uint32_t>(ASTL_NO_CACHING) : 0U,
                     .sampling_interval = 0, .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE);
    REQUIRE(orchestrator->ConfigureMetricCollection(target, &config, {&metric, 1}) == ASTL_STATUS_SUCCESS);
    auto clocks = manager->GetClockCorrelations();
    REQUIRE_FALSE(clocks.empty());
    // Fixed correlation makes timestamp regressions deterministic without wall-clock timing.
    for (auto& [id, clock] : clocks) {
      (void)id;
      clock = astl::OperationClockCorrelation{
          astl::ProcessedSampleTimestamp{},
          0, astl::NativeToMonotonicRawRatio{1, 1}
      };
    }
    manager->SetClockCorrelations(clocks);
  }

  void Read(uint64_t timestamp, uint64_t value = 42) const {
    scmi_tlm_de_sample sample{};
    sample.id     = 1;
    sample.tstamp = timestamp;
    sample.val    = value;
    ReadSample(sample, ASTL_STATUS_SUCCESS);
  }

  void ReadSample(const scmi_tlm_de_sample& sample, astl_status_code status) const {
    if (single_read) {
      REQUIRE_CALL(*device, ReadSingle(_, _)).LR_SIDE_EFFECT(_1[0] = sample).SIDE_EFFECT(_2 = 1).RETURN(status);
      REQUIRE(collector->ReadImmediate() == status);
    } else {
      REQUIRE_CALL(*device, ReadDataEventValue(1, _)).LR_SIDE_EFFECT(_2 = sample).RETURN(status);
      REQUIRE(collector->ReadImmediate() == status);
    }
  }

  void Expect(uint32_t expected_count, astl_status_code expected_status, uint64_t start = 0) const {
    std::array<astl_sample_t, 8> samples{};
    uint32_t                     count = static_cast<uint32_t>(samples.size());
    ASTL_INIT_STRUCT(astl_get_metric_samples_on_target_params_t, params, .flags = 0, .target_handle = target,
                     .metric_handle = metric, .samples = samples.data(), .sample_count = &count, .start_ts = start,
                     .end_ts = 0);
    REQUIRE(astlGetMetricSamplesOnTarget(&params) == expected_status);
    CHECK(count == expected_count);
  }
};
}  // namespace

TEST_CASE("Empty statuses propagate from SCMI to the C API", "[empty_reason][wrapper][scmi]") {
  EmptyResultFixture fixture;
  fixture.single_read = GENERATE(false, true);
  fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED);
  fixture.Read(100);
  fixture.Expect(1, ASTL_STATUS_SUCCESS);
  fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED);  // consumed data has no retained reason
  fixture.Read(100);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);  // diagnostics are not consumed
  fixture.Read(90);
  fixture.Expect(0, ASTL_STATUS_SAMPLE_DROPPED);
  fixture.Read(110);
  fixture.Expect(1, ASTL_STATUS_SUCCESS);
  fixture.Read(110);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
  fixture.ReadSample({}, ASTL_STATUS_INTERNAL_ERROR);
  fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED);
  fixture.Read(110);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
  fixture.Configure();
  fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED);
}

TEST_CASE("Delta and rate warm-up report no new sample", "[empty_reason][wrapper][scmi]") {
  const auto         type = GENERATE(ASTL_METRIC_DELTA, ASTL_METRIC_RATE);
  EmptyResultFixture fixture(type);
  fixture.Read(1000000, 10);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
  fixture.Read(1000000, 11);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
  fixture.Read(2000000, 20);
  fixture.Expect(1, ASTL_STATUS_SUCCESS);
}

TEST_CASE("Empty statuses do not misattribute filtered or cached data", "[empty_reason][wrapper]") {
  SECTION("Filtered-out samples") {
    EmptyResultFixture fixture;
    fixture.Read(100);
    fixture.Read(100);  // latest input was a duplicate but a valid sample remains
    fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED, 200);
    fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED);  // filtered retrieval consumed the available sample
  }
  SECTION("Cached collection") {
    EmptyResultFixture fixture(ASTL_METRIC_VALUE, false);
    fixture.Read(100);
    fixture.Read(100);
    fixture.Expect(0, ASTL_STATUS_NO_DATA_COLLECTED, 200);
  }
}

// These assertions verify outputs changed through pointers by the C API. Cppcheck cannot follow those writes.
// cppcheck-suppress-begin knownConditionTrueFalse
TEST_CASE("Count queries retain empty success", "[empty_reason][wrapper]") {
  EmptyResultFixture fixture;
  fixture.Read(100);
  fixture.Expect(1, ASTL_STATUS_SUCCESS);
  fixture.Read(100);  // A metric-specific diagnostic must not change the count query.
  uint32_t count = 99;
  ASTL_INIT_STRUCT(astl_get_metric_sample_count_on_target_params_t, query, .flags = 0, .target_handle = fixture.target,
                   .metric_handle = fixture.metric, .sample_count = &count, .start_ts = 0, .end_ts = 0);
  REQUIRE(astlGetMetricSampleCountOnTarget(&query) == ASTL_STATUS_SUCCESS);
  CHECK(count == 0);
  fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
}

TEST_CASE("Empty status parameter validation preserves errors", "[empty_reason][wrapper]") {
  EmptyResultFixture fixture;
  astl_sample_t      sample{};
  uint32_t           count = 1;
  ASTL_INIT_STRUCT(astl_get_metric_samples_on_target_params_t, params, .flags = 0, .target_handle = fixture.target,
                   .metric_handle = fixture.metric, .samples = &sample, .sample_count = &count, .start_ts = 0,
                   .end_ts = 0);
  SECTION("Null sample buffer") {
    params.samples = nullptr;
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Zero capacity") {
    count = 0;
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Reserved flag") {
    params.flags = GENERATE(1U, 2U);
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }
  SECTION("Undersized struct") {
    params.size = sizeof(params) - 1;
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }
  SECTION("Oversized struct") {
    params.size = sizeof(params) + 1;
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
  SECTION("Invalid range") {
    params.start_ts = 20;
    params.end_ts   = 10;
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Insufficient capacity") {
    fixture.Read(100);
    fixture.Read(200);
    CHECK(astlGetMetricSamplesOnTarget(&params) == ASTL_STATUS_BUFFER_TOO_SMALL);
    CHECK(count == 2);
    fixture.Expect(2, ASTL_STATUS_SUCCESS);
  }
}

// cppcheck-suppress-end knownConditionTrueFalse

TEST_CASE("ReadImmediate reports raw collection independently of metric output", "[wrapper][empty_result]") {
  const bool         uncached    = GENERATE(true, false);
  const bool         single_read = GENERATE(true, false);
  EmptyResultFixture fixture(ASTL_METRIC_DELTA, uncached);
  fixture.single_read = single_read;
  ASTL_INIT_STRUCT(astl_read_immediate_on_target_params_t, params, .flags = 0, .target_handle = fixture.target);
  auto read = [&](uint64_t timestamp, astl_status_code expected) {
    scmi_tlm_de_sample sample{};
    sample.id     = 1;
    sample.tstamp = timestamp;
    sample.val    = timestamp;
    if (single_read) {
      REQUIRE_CALL(*fixture.device, ReadSingle(_, _))
          .LR_SIDE_EFFECT(_1[0] = sample)
          .SIDE_EFFECT(_2 = 1)
          .RETURN(ASTL_STATUS_SUCCESS);
      CHECK(astlReadImmediateOnTarget(&params) == expected);
    } else {
      REQUIRE_CALL(*fixture.device, ReadDataEventValue(1, _)).LR_SIDE_EFFECT(_2 = sample).RETURN(ASTL_STATUS_SUCCESS);
      CHECK(astlReadImmediateOnTarget(&params) == expected);
    }
  };
  read(100, ASTL_STATUS_SUCCESS);  // Baseline is raw input, despite no delta output.
  if (uncached) {
    fixture.Expect(0, ASTL_STATUS_NO_NEW_SAMPLE);
  }
  read(100, ASTL_STATUS_NO_NEW_SAMPLE);
  read(200, ASTL_STATUS_SUCCESS);
  read(150, ASTL_STATUS_SUCCESS);  // Metric-stage rejection does not undo raw collection.
  if (uncached) {
    fixture.Expect(1, ASTL_STATUS_SUCCESS);
  }
}
