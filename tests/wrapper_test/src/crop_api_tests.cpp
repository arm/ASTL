// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crop_api_tests.cpp
 * @brief Tests for the astlCropSamplesOnTarget, astlCropMetricSamplesOnTarget, and astlCropSamples C APIs.
 *
 * Coverage:
 *  - NULL params pointer
 *  - Incompatible params struct size
 *  - Non-zero params flags
 *  - Window array validation (NULL, window_count == 0, wrong windows[0].size,
 *    non-zero window flags, start_ts > end_ts)
 *  - Valid call with a sentinel (unregistered) target handle returns ASTL_STATUS_INVALID_TARGET_HANDLE
 *  - Valid call to astlCropSamples with an empty target list returns ASTL_STATUS_SUCCESS
 *  - End-to-end: processed samples are filtered to the keep window
 *  - End-to-end: on-disk raw sample cache file is filtered to the keep window
 *  - End-to-end: astlCropMetricSamplesOnTarget only filters the specified metric
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "common/clock_correlation.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "metric/metric_manager.hpp"
#include "operation/operation.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "target.hpp"

using expectation = std::unique_ptr<trompeloeil::expectation>;
using trompeloeil::_;

// ---------------------------------------------------------------------------
// Sentinel handles used throughout
// ---------------------------------------------------------------------------
namespace {
const int                  kSentinelTarget{};
const int                  kSentinelMetric{};
astl_target_handle_t const kTarget = static_cast<astl_target_handle_t>(&kSentinelTarget);
astl_metric_handle_t const kMetric = static_cast<astl_metric_handle_t>(&kSentinelMetric);
}  // namespace

// ===========================================================================
// astlCropSamplesOnTarget
// ===========================================================================

TEST_CASE("astlCropSamplesOnTarget - NULL params", "[crop_api]") {
  REQUIRE(astlCropSamplesOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlCropSamplesOnTarget - incompatible struct size", "[crop_api]") {
  astl_crop_window_t                   window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_on_target_params_t params{};
  params.target_handle = kTarget;
  params.windows       = &window;
  params.window_count  = 1;
  params.flags         = 0;

  SECTION("size too small") {
    params.size = sizeof(astl_crop_samples_on_target_params_t) - 1;
    REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("size too large") {
    params.size = sizeof(astl_crop_samples_on_target_params_t) + 1;
    REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
}

TEST_CASE("astlCropSamplesOnTarget - non-zero params flags", "[crop_api]") {
  astl_crop_window_t                   window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_samples_on_target_params_t);
  params.flags         = 1U;
  params.target_handle = kTarget;
  params.windows       = &window;
  params.window_count  = 1;
  REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlCropSamplesOnTarget - valid params with unregistered target returns INVALID_TARGET_HANDLE",
          "[crop_api]") {
  REQUIRE(CropSamplesOnTarget(kTarget, 0, 0) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE(
    "astlCropSamplesOnTarget - valid params with non-zero window bounds and unregistered target returns "
    "INVALID_TARGET_HANDLE",
    "[crop_api]") {
  REQUIRE(CropSamplesOnTarget(kTarget, 1'000'000, 5'000'000) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

// ===========================================================================
// astlCropMetricSamplesOnTarget
// ===========================================================================

TEST_CASE("astlCropMetricSamplesOnTarget - NULL params", "[crop_api]") {
  REQUIRE(astlCropMetricSamplesOnTarget(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlCropMetricSamplesOnTarget - incompatible struct size", "[crop_api]") {
  astl_crop_window_t                          window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_metric_samples_on_target_params_t params{};
  params.target_handle = kTarget;
  params.metric_handle = kMetric;
  params.windows       = &window;
  params.window_count  = 1;
  params.flags         = 0;

  SECTION("size too small") {
    params.size = sizeof(astl_crop_metric_samples_on_target_params_t) - 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("size too large") {
    params.size = sizeof(astl_crop_metric_samples_on_target_params_t) + 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
}

TEST_CASE("astlCropMetricSamplesOnTarget - non-zero params flags", "[crop_api]") {
  astl_crop_window_t                          window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_metric_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_metric_samples_on_target_params_t);
  params.flags         = 1U;
  params.target_handle = kTarget;
  params.metric_handle = kMetric;
  params.windows       = &window;
  params.window_count  = 1;
  REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlCropMetricSamplesOnTarget - window array validation", "[crop_api]") {
  astl_crop_metric_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_metric_samples_on_target_params_t);
  params.flags         = 0;
  params.target_handle = kTarget;
  params.metric_handle = kMetric;

  SECTION("NULL windows pointer") {
    params.windows      = nullptr;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("window_count is zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, 0, 0};
    params.windows      = &window;
    params.window_count = 0;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("windows[0].size is wrong") {
    astl_crop_window_t window{sizeof(astl_crop_window_t) - 1, 0, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("windows[0].flags is non-zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), /*flags=*/1U, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("start_ts > end_ts (both non-zero)") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/5'000'000, /*end_ts=*/1'000'000};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("astlCropMetricSamplesOnTarget - valid params with unregistered target returns INVALID_TARGET_HANDLE",
          "[crop_api]") {
  REQUIRE(CropMetricSamplesOnTarget(kTarget, kMetric, 0, 0) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE(
    "astlCropMetricSamplesOnTarget - valid params with non-zero window bounds and unregistered target returns "
    "INVALID_TARGET_HANDLE",
    "[crop_api]") {
  REQUIRE(CropMetricSamplesOnTarget(kTarget, kMetric, 1'000'000, 5'000'000) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

TEST_CASE(
    "astlCropMetricSamplesOnTarget - start_ts == end_ts (single-point window) with unregistered target returns "
    "INVALID_TARGET_HANDLE",
    "[crop_api]") {
  REQUIRE(CropMetricSamplesOnTarget(kTarget, kMetric, 3'000'000, 3'000'000) == ASTL_STATUS_INVALID_TARGET_HANDLE);
}

// ===========================================================================
// astlCropSamples
// ===========================================================================

TEST_CASE("astlCropSamples - NULL params", "[crop_api]") {
  REQUIRE(astlCropSamples(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlCropSamples - incompatible struct size", "[crop_api]") {
  astl_crop_window_t         window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_params_t params{};
  params.windows      = &window;
  params.window_count = 1;
  params.flags        = 0;

  SECTION("size too small") {
    params.size = sizeof(astl_crop_samples_params_t) - 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_OLD_STRUCT_VERSION);
  }

  SECTION("size too large") {
    params.size = sizeof(astl_crop_samples_params_t) + 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }
}

TEST_CASE("astlCropSamples - non-zero params flags", "[crop_api]") {
  astl_crop_window_t         window{sizeof(astl_crop_window_t), 0, 0, 0};
  astl_crop_samples_params_t params{};
  params.size         = sizeof(astl_crop_samples_params_t);
  params.flags        = 1U;
  params.windows      = &window;
  params.window_count = 1;
  REQUIRE(astlCropSamples(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
}

TEST_CASE("astlCropSamples - window array validation", "[crop_api]") {
  astl_crop_samples_params_t params{};
  params.size  = sizeof(astl_crop_samples_params_t);
  params.flags = 0;

  SECTION("NULL windows pointer") {
    params.windows      = nullptr;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("window_count is zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, 0, 0};
    params.windows      = &window;
    params.window_count = 0;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("windows[0].size is wrong") {
    astl_crop_window_t window{sizeof(astl_crop_window_t) + 1, 0, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_NEW_STRUCT_VERSION);
  }

  SECTION("windows[0].flags is non-zero") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), /*flags=*/1U, 0, 0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_INVALID_FLAG_VALUE);
  }

  SECTION("start_ts > end_ts (both non-zero)") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/9'000'000, /*end_ts=*/2'000'000};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("start_ts == 0 with non-zero end_ts (no lower bound) is valid") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/0, /*end_ts=*/5'000'000};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_SUCCESS);
  }

  SECTION("end_ts == 0 with non-zero start_ts (no upper bound) is valid") {
    astl_crop_window_t window{sizeof(astl_crop_window_t), 0, /*start_ts=*/1'000'000, /*end_ts=*/0};
    params.windows      = &window;
    params.window_count = 1;
    REQUIRE(astlCropSamples(&params) == ASTL_STATUS_SUCCESS);
  }
}

TEST_CASE("astlCropSamples - valid params succeeds", "[crop_api]") {
  REQUIRE(CropSamples(0, 0) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlCropSamples - valid params with non-zero window bounds succeeds", "[crop_api]") {
  REQUIRE(CropSamples(1'000'000, 5'000'000) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("astlCropSamples - multiple windows all valid succeeds", "[crop_api]") {
  std::array<astl_crop_window_t, 3> windows{};
  windows[0] = {sizeof(astl_crop_window_t), 0, 1'000'000, 2'000'000};
  windows[1] = {sizeof(astl_crop_window_t), 0, 4'000'000, 6'000'000};
  windows[2] = {sizeof(astl_crop_window_t), 0, 0, 8'000'000};

  astl_crop_samples_params_t params{};
  params.size         = sizeof(astl_crop_samples_params_t);
  params.flags        = 0;
  params.windows      = windows.data();
  params.window_count = static_cast<uint32_t>(windows.size());
  REQUIRE(astlCropSamples(&params) == ASTL_STATUS_SUCCESS);
}

// ===========================================================================
// End-to-end crop tests: verify processed and raw samples are filtered
// ===========================================================================

namespace {

/// Creates a ready-to-use test orchestrator for crop end-to-end tests.
/// Does NOT set up GetClockCorrelations; callers must add that expectation.
auto BuildCropOrchestrator(std::vector<expectation>& out_expectations, const std::filesystem::path& cache_dir = "")
    -> std::tuple<std::unique_ptr<astl::Orchestrator>, MockMetricManager*, MockCollectorManager*> {
  auto  topology_manager  = std::make_unique<MockTopologyManager>();
  auto  collector_manager = std::make_unique<MockCollectorManager>();
  auto* cm_raw            = collector_manager.get();
  out_expectations.push_back(
      NAMED_ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  out_expectations.push_back(
      NAMED_ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  auto  mock_mm = std::make_unique<MockMetricManager>();
  auto* mm_raw  = mock_mm.get();
  out_expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  out_expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  out_expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, RemoveAllMetrics()));
  out_expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr));
  out_expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, InjectLifecycleEvent(_, _, _)).RETURN(ASTL_STATUS_SUCCESS));
  // CropSamplesOnTarget lazily registers the lifecycle-event metric before injecting crop events.
  out_expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS));
  out_expectations.push_back(
      NAMED_ALLOW_CALL(*mm_raw, GetAvailableMetrics(_)).RETURN(std::span<const astl_metric_handle_t>{}));
  out_expectations.push_back(
      NAMED_ALLOW_CALL(*mm_raw, GetAvailableCounters(_)).RETURN(std::span<const astl_counter_handle_t>{}));
  auto output_manager = std::make_unique<astl::OutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(mock_mm), std::move(output_manager), cache_dir);
  return {std::move(orchestrator), mm_raw, cm_raw};
}

/// Returns a ProcessedSampledData with the given timestamp (nanoseconds) and value 1.
inline auto MakeProcSample(uint64_t ts_ns) -> astl::ProcessedSampledData {
  return {astl::AstlValue{uint64_t{1}},
          astl::ProcessedSampleTimestamp{std::chrono::duration<int64_t, std::nano>{static_cast<int64_t>(ts_ns)}}};
}

/// Returns a RawSampledData whose raw_tick == ts_ns. Requires the identity
/// ClockCorrelation (1 tick == 1 ns) to be registered for op_id.
inline auto MakeRawSample(astl::OperationId op_id, uint64_t ts_ns) -> astl::RawSampledData {
  return astl::RawSampledData{op_id, astl::AstlValue{uint64_t{42}}, ts_ns};
}

/// Returns a ClockCorrelationMap where 1 native tick == 1 nanosecond for op_id.
inline auto IdentityCorrelation(astl::OperationId op_id) -> astl::ClockCorrelationMap {
  astl::ClockCorrelationMap corrs;
  corrs[op_id] = astl::OperationClockCorrelation{
      astl::ProcessedSampleTimestamp{std::chrono::duration<int64_t, std::nano>{0}},
      astl::HwClockTicks{0},
      astl::NativeToMonotonicRawRatio{1, 1}
  };
  return corrs;
}

/// Reads and deserialises the raw sample cache file at path.
inline auto ReadRawCacheFile(const std::filesystem::path& path) -> std::vector<astl::RawSampledData> {
  std::ifstream input_stream(path, std::ios::binary);
  REQUIRE(input_stream.good());
  auto result = astl::ProtobufSerDes::Deserialize<std::vector<astl::RawSampledData>>(input_stream);
  REQUIRE(result.has_value());
  return std::move(*result);
}

/// Fixture for tests that need a single-target, single-metric orchestrator with three
/// processed samples pre-sunk at 100, 200, and 300 nanoseconds.
struct SingleMetricCropFixture {
  std::vector<expectation>                expectations;
  std::unique_ptr<astl::MetricHandle>     mh_storage{std::make_unique<astl::MetricHandle>()};
  TestMetricBase                          metric;
  astl_metric_handle_t                    metric_hdl;
  std::unique_ptr<TestTargetBase>         target_storage;
  TestTargetBase*                         target_ptr;
  astl_target_handle_t                    target_hdl;
  astl::Orchestrator*                     orch_raw{};
  std::optional<TestOrchestratorInjector> injector;

  SingleMetricCropFixture(std::string target_name, std::string metric_name)
      : metric{std::move(metric_name)},
        metric_hdl{static_cast<astl_metric_handle_t>(mh_storage.get())},
        target_storage{std::make_unique<TestTargetBase>(std::move(target_name))},
        target_ptr{target_storage.get()},
        target_hdl{static_cast<astl_target_handle_t>(target_ptr)} {
    auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations);
    (void)cm_raw;
    expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetClockCorrelations()).RETURN(astl::ClockCorrelationMap{}));

    astl::IMetric* metric_iface = &metric;
    expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl, target_ptr)).RETURN(metric_iface));

    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(std::move(target_storage));
    REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
    orch_raw = orchestrator.get();

    const std::vector<astl::ProcessedSampledData> samples{MakeProcSample(100), MakeProcSample(200),
                                                          MakeProcSample(300)};
    REQUIRE(orch_raw->SinkProcessedSamples(target_ptr, &metric, samples) == ASTL_STATUS_SUCCESS);
    injector.emplace(std::move(orchestrator));
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Processed-sample filtering
// ---------------------------------------------------------------------------

TEST_CASE("astlCropSamplesOnTarget - out-of-window processed samples are removed", "[crop_api][e2e]") {
  std::vector<expectation> expectations;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations);
  (void)cm_raw;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetClockCorrelations()).RETURN(astl::ClockCorrelationMap{}));

  auto        target_uptr = std::make_unique<TestTargetBase>("crop-e2e-proc-target");
  auto*       target_ptr  = target_uptr.get();
  const auto* target_hdl  = static_cast<astl_target_handle_t>(target_ptr);

  auto                 mh_storage = std::make_unique<astl::MetricHandle>();
  astl_metric_handle_t metric_hdl = static_cast<astl_metric_handle_t>(mh_storage.get());
  TestMetricBase       metric{"crop-e2e-proc-metric"};
  astl::IMetric*       metric_iface = &metric;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl, target_ptr)).RETURN(metric_iface));

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto*                    orch_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Sink 5 processed samples at 100, 200, 300, 400, 500 ns
  const std::vector<astl::ProcessedSampledData> samples{MakeProcSample(100), MakeProcSample(200), MakeProcSample(300),
                                                        MakeProcSample(400), MakeProcSample(500)};
  REQUIRE(orch_raw->SinkProcessedSamples(target_ptr, &metric, samples) == ASTL_STATUS_SUCCESS);

  // Crop: keep window [200 ns, 400 ns] — 3 samples should survive
  REQUIRE(CropSamplesOnTarget(target_hdl, 200, 400) == ASTL_STATUS_SUCCESS);

  uint32_t count = 0;
  REQUIRE(GetMetricSampleCountOnTarget(target_hdl, metric_hdl, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 3);
}

TEST_CASE("astlCropSamplesOnTarget - overlapping windows retain the consolidated ranges", "[crop_api][e2e]") {
  std::vector<expectation> expectations;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations);
  (void)cm_raw;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetClockCorrelations()).RETURN(astl::ClockCorrelationMap{}));

  auto        target_uptr = std::make_unique<TestTargetBase>("crop-e2e-overlap-target");
  auto*       target_ptr  = target_uptr.get();
  const auto* target_hdl  = static_cast<astl_target_handle_t>(target_ptr);

  auto                 mh_storage = std::make_unique<astl::MetricHandle>();
  astl_metric_handle_t metric_hdl = static_cast<astl_metric_handle_t>(mh_storage.get());
  TestMetricBase       metric{"crop-e2e-overlap-metric"};
  astl::IMetric*       metric_iface = &metric;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl, target_ptr)).RETURN(metric_iface));

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto*                    orch_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl::ProcessedSampledData> samples;
  for (uint64_t ts_ns = 1; ts_ns <= 15; ++ts_ns) {
    samples.push_back(MakeProcSample(ts_ns));
  }
  REQUIRE(orch_raw->SinkProcessedSamples(target_ptr, &metric, samples) == ASTL_STATUS_SUCCESS);

  std::array<astl_crop_window_t, 4> windows{};
  windows[0] = {sizeof(astl_crop_window_t), 0, 1, 3};
  windows[1] = {sizeof(astl_crop_window_t), 0, 2, 3};
  windows[2] = {sizeof(astl_crop_window_t), 0, 7, 9};
  windows[3] = {sizeof(astl_crop_window_t), 0, 11, 15};

  astl_crop_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_samples_on_target_params_t);
  params.flags         = 0;
  params.target_handle = target_hdl;
  params.windows       = windows.data();
  params.window_count  = static_cast<uint32_t>(windows.size());
  REQUIRE(astlCropSamplesOnTarget(&params) == ASTL_STATUS_SUCCESS);

  uint32_t count = 0;
  REQUIRE(GetMetricSampleCountOnTarget(target_hdl, metric_hdl, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 11);

  std::array<astl_sample_t, 11> retained_samples{};
  REQUIRE(GetMetricSamplesOnTarget(target_hdl, metric_hdl, retained_samples.data(), &count) == ASTL_STATUS_SUCCESS);
  const std::array<uint64_t, 11> expected_timestamps{1, 2, 3, 7, 8, 9, 11, 12, 13, 14, 15};
  REQUIRE(count == expected_timestamps.size());
  REQUIRE(std::equal(
      retained_samples.begin(), retained_samples.end(), expected_timestamps.begin(), expected_timestamps.end(),
      [](const auto& sample, uint64_t expected_timestamp) { return sample.timestamp == expected_timestamp; }));
}

TEST_CASE("astlCropSamplesOnTarget - open lower bound (start_ts=0) retains samples up to end_ts", "[crop_api][e2e]") {
  SingleMetricCropFixture fixture{"crop-e2e-lower-target", "crop-e2e-lower-metric"};
  // Crop: keep [0 (open), 200 ns] — 2 samples survive (100, 200)
  REQUIRE(CropSamplesOnTarget(fixture.target_hdl, 0, 200) == ASTL_STATUS_SUCCESS);
  uint32_t count = 0;
  REQUIRE(GetMetricSampleCountOnTarget(fixture.target_hdl, fixture.metric_hdl, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 2);
}

TEST_CASE("astlCropSamplesOnTarget - open upper bound (end_ts=0) retains samples from start_ts onwards",
          "[crop_api][e2e]") {
  SingleMetricCropFixture fixture{"crop-e2e-upper-target", "crop-e2e-upper-metric"};
  // Crop: keep [200 ns, 0 (open)] — 2 samples survive (200, 300)
  REQUIRE(CropSamplesOnTarget(fixture.target_hdl, 200, 0) == ASTL_STATUS_SUCCESS);
  uint32_t count = 0;
  REQUIRE(GetMetricSampleCountOnTarget(fixture.target_hdl, fixture.metric_hdl, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 2);
}

TEST_CASE("astlCropSamplesOnTarget - all samples outside window leaves empty processed set", "[crop_api][e2e]") {
  SingleMetricCropFixture fixture{"crop-e2e-empty-target", "crop-e2e-empty-metric"};
  // Crop: keep [500 ns, 600 ns] — none of 100/200/300 survive
  REQUIRE(CropSamplesOnTarget(fixture.target_hdl, 500, 600) == ASTL_STATUS_SUCCESS);
  uint32_t count = 99;
  REQUIRE(GetMetricSampleCountOnTarget(fixture.target_hdl, fixture.metric_hdl, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 0);
}

// ---------------------------------------------------------------------------
// Raw sample cache-file filtering
// ---------------------------------------------------------------------------

TEST_CASE("astlCropSamplesOnTarget - on-disk raw sample cache file is filtered to the keep window", "[crop_api][e2e]") {
  namespace fs              = std::filesystem;
  const fs::path  cache_dir = fs::temp_directory_path() / "astl_crop_raw_filter_test";
  TempFileGuard   cache_guard(cache_dir);
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);

  std::vector<expectation> expectations;
  const auto               op_id      = astl::kFirstAssignableOperationId;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations, cache_dir);
  (void)cm_raw;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetClockCorrelations()).RETURN(IdentityCorrelation(op_id)));

  auto        target_uptr = std::make_unique<TestTargetBase>("crop-e2e-raw-target");
  auto*       target_ptr  = target_uptr.get();
  const auto* target_hdl  = static_cast<astl_target_handle_t>(target_ptr);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Write raw samples at 100, 200, 300, 400, 500 ns directly to the cache file
  const std::vector<astl::RawSampledData> raw_samples{MakeRawSample(op_id, 100), MakeRawSample(op_id, 200),
                                                      MakeRawSample(op_id, 300), MakeRawSample(op_id, 400),
                                                      MakeRawSample(op_id, 500)};
  const fs::path cache_file = cache_dir / (astl::GetStableTargetKey(*target_ptr) + astl::kAstlFileExtension);
  {
    std::ofstream out(cache_file, std::ios::binary);
    REQUIRE(out.good());
    REQUIRE(astl::ProtobufSerDes::Serialize(raw_samples, out) == ASTL_STATUS_SUCCESS);
  }

  // Crop: keep [200 ns, 400 ns] — 3 raw samples should survive (200, 300, 400)
  REQUIRE(CropSamplesOnTarget(target_hdl, 200, 400) == ASTL_STATUS_SUCCESS);

  REQUIRE(fs::exists(cache_file));
  const auto filtered = ReadRawCacheFile(cache_file);
  REQUIRE(filtered.size() == 3);
}

TEST_CASE("astlCropSamplesOnTarget - raw sample cache file is removed when all samples fall outside keep window",
          "[crop_api][e2e]") {
  namespace fs              = std::filesystem;
  const fs::path  cache_dir = fs::temp_directory_path() / "astl_crop_raw_delete_test";
  TempFileGuard   cache_guard(cache_dir);
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);

  std::vector<expectation> expectations;
  const auto               op_id      = astl::kFirstAssignableOperationId;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations, cache_dir);
  (void)cm_raw;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetClockCorrelations()).RETURN(IdentityCorrelation(op_id)));

  auto        target_uptr = std::make_unique<TestTargetBase>("crop-e2e-raw-del-target");
  auto*       target_ptr  = target_uptr.get();
  const auto* target_hdl  = static_cast<astl_target_handle_t>(target_ptr);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Write raw samples entirely outside the crop window
  const std::vector<astl::RawSampledData> raw_samples{MakeRawSample(op_id, 100), MakeRawSample(op_id, 200),
                                                      MakeRawSample(op_id, 300)};
  const fs::path cache_file = cache_dir / (astl::GetStableTargetKey(*target_ptr) + astl::kAstlFileExtension);
  {
    std::ofstream out(cache_file, std::ios::binary);
    REQUIRE(out.good());
    REQUIRE(astl::ProtobufSerDes::Serialize(raw_samples, out) == ASTL_STATUS_SUCCESS);
  }

  // Crop: keep [500 ns, 600 ns] — all samples are outside; file should be deleted
  REQUIRE(CropSamplesOnTarget(target_hdl, 500, 600) == ASTL_STATUS_SUCCESS);

  REQUIRE_FALSE(fs::exists(cache_file));
}

// ---------------------------------------------------------------------------
// CropMetricSamplesOnTarget metric isolation
// ---------------------------------------------------------------------------

TEST_CASE("astlCropMetricSamplesOnTarget - only the specified metric's samples are cropped", "[crop_api][e2e]") {
  std::vector<expectation> expectations;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations);
  (void)cm_raw;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetClockCorrelations()).RETURN(astl::ClockCorrelationMap{}));

  auto        target_uptr = std::make_unique<TestTargetBase>("crop-e2e-two-metric-target");
  auto*       target_ptr  = target_uptr.get();
  const auto* target_hdl  = static_cast<astl_target_handle_t>(target_ptr);

  // Metric A — will be cropped
  auto                 mh_a_storage = std::make_unique<astl::MetricHandle>();
  astl_metric_handle_t metric_hdl_a = static_cast<astl_metric_handle_t>(mh_a_storage.get());
  TestMetricBase       metric_a{"crop-e2e-metric-a"};
  astl::IMetric*       metric_a_iface = &metric_a;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl_a, target_ptr)).RETURN(metric_a_iface));

  // Metric B — must stay untouched
  auto                 mh_b_storage = std::make_unique<astl::MetricHandle>();
  astl_metric_handle_t metric_hdl_b = static_cast<astl_metric_handle_t>(mh_b_storage.get());
  TestMetricBase       metric_b{"crop-e2e-metric-b"};
  astl::IMetric*       metric_b_iface = &metric_b;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl_b, target_ptr)).RETURN(metric_b_iface));

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto*                    orch_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  const std::vector<astl::ProcessedSampledData> samples_abc{MakeProcSample(100), MakeProcSample(200),
                                                            MakeProcSample(300)};
  REQUIRE(orch_raw->SinkProcessedSamples(target_ptr, &metric_a, samples_abc) == ASTL_STATUS_SUCCESS);
  REQUIRE(orch_raw->SinkProcessedSamples(target_ptr, &metric_b, samples_abc) == ASTL_STATUS_SUCCESS);

  // Crop metric A to only keep [200 ns, 200 ns] — 1 sample survives
  REQUIRE(CropMetricSamplesOnTarget(target_hdl, metric_hdl_a, 200, 200) == ASTL_STATUS_SUCCESS);

  uint32_t count_a = 0;
  REQUIRE(GetMetricSampleCountOnTarget(target_hdl, metric_hdl_a, &count_a) == ASTL_STATUS_SUCCESS);
  REQUIRE(count_a == 1);

  // Metric B must still have all 3 samples
  uint32_t count_b = 0;
  REQUIRE(GetMetricSampleCountOnTarget(target_hdl, metric_hdl_b, &count_b) == ASTL_STATUS_SUCCESS);
  REQUIRE(count_b == 3);
}

TEST_CASE("astlCropMetricSamplesOnTarget - overlapping windows retain the consolidated ranges", "[crop_api][e2e]") {
  std::vector<expectation> expectations;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations);
  (void)cm_raw;

  auto        target_uptr = std::make_unique<TestTargetBase>("crop-e2e-metric-overlap-target");
  auto*       target_ptr  = target_uptr.get();
  const auto* target_hdl  = static_cast<astl_target_handle_t>(target_ptr);

  auto                 mh_storage = std::make_unique<astl::MetricHandle>();
  astl_metric_handle_t metric_hdl = static_cast<astl_metric_handle_t>(mh_storage.get());
  TestMetricBase       metric{"crop-e2e-metric-overlap"};
  astl::IMetric*       metric_iface = &metric;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl, target_ptr)).RETURN(metric_iface));

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto*                    orch_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl::ProcessedSampledData> samples;
  for (uint64_t ts_ns = 1; ts_ns <= 15; ++ts_ns) {
    samples.push_back(MakeProcSample(ts_ns));
  }
  REQUIRE(orch_raw->SinkProcessedSamples(target_ptr, &metric, samples) == ASTL_STATUS_SUCCESS);

  std::array<astl_crop_window_t, 4> windows{};
  windows[0] = {sizeof(astl_crop_window_t), 0, 1, 3};
  windows[1] = {sizeof(astl_crop_window_t), 0, 2, 3};
  windows[2] = {sizeof(astl_crop_window_t), 0, 7, 9};
  windows[3] = {sizeof(astl_crop_window_t), 0, 11, 15};

  astl_crop_metric_samples_on_target_params_t params{};
  params.size          = sizeof(astl_crop_metric_samples_on_target_params_t);
  params.flags         = 0;
  params.target_handle = target_hdl;
  params.metric_handle = metric_hdl;
  params.windows       = windows.data();
  params.window_count  = static_cast<uint32_t>(windows.size());
  REQUIRE(astlCropMetricSamplesOnTarget(&params) == ASTL_STATUS_SUCCESS);

  uint32_t count = 0;
  REQUIRE(GetMetricSampleCountOnTarget(target_hdl, metric_hdl, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 11);

  std::array<astl_sample_t, 11> retained_samples{};
  REQUIRE(GetMetricSamplesOnTarget(target_hdl, metric_hdl, retained_samples.data(), &count) == ASTL_STATUS_SUCCESS);
  const std::array<uint64_t, 11> expected_timestamps{1, 2, 3, 7, 8, 9, 11, 12, 13, 14, 15};
  REQUIRE(count == expected_timestamps.size());
  REQUIRE(std::equal(
      retained_samples.begin(), retained_samples.end(), expected_timestamps.begin(), expected_timestamps.end(),
      [](const auto& sample, uint64_t expected_timestamp) { return sample.timestamp == expected_timestamp; }));
}

TEST_CASE(
    "astlCropSamples - returns COLLECTION_NOT_STOPPED when any target is STARTED and leaves other targets untouched",
    "[crop_api][e2e]") {
  // Two targets: target_stopped (UNCONFIGURED, has 5 processed samples) and
  // target_active (STARTED via ConfigureMetricCollection + StartCollection).
  // CropSamples must return COLLECTION_NOT_STOPPED and leave target_stopped's samples intact.
  std::vector<expectation> expectations;
  auto [orchestrator, mm_raw, cm_raw] = BuildCropOrchestrator(expectations);
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS));

  auto        target_stopped_uptr = std::make_unique<TestTargetBase>("crop-stopped-target");
  auto*       target_stopped_ptr  = target_stopped_uptr.get();
  const auto* target_stopped_hdl  = static_cast<astl_target_handle_t>(target_stopped_ptr);

  auto  target_active_uptr = std::make_unique<TestTargetBase>("crop-active-target");
  auto* target_active_ptr  = target_active_uptr.get();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(target_stopped_uptr));
  targets.push_back(std::move(target_active_uptr));
  REQUIRE(orchestrator->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  auto* orch_raw = orchestrator.get();

  // Sink 5 processed samples on the stopped target
  auto                 mh_stopped         = std::make_unique<astl::MetricHandle>();
  astl_metric_handle_t metric_hdl_stopped = static_cast<astl_metric_handle_t>(mh_stopped.get());
  TestMetricBase       stopped_metric{"crop-stopped-metric"};
  astl::IMetric*       stopped_metric_iface = &stopped_metric;
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_hdl_stopped, target_stopped_ptr))
                             .RETURN(stopped_metric_iface));
  const std::vector<astl::ProcessedSampledData> proc_samples{
      MakeProcSample(100), MakeProcSample(200), MakeProcSample(300), MakeProcSample(400), MakeProcSample(500)};
  REQUIRE(orch_raw->SinkProcessedSamples(target_stopped_ptr, &stopped_metric, proc_samples) == ASTL_STATUS_SUCCESS);

  // Bring target_active to STARTED state via ConfigureMetricCollection + StartCollection
  static int                        dummy_active_metric_storage{};
  astl_metric_handle_t              active_metric_handle = &dummy_active_metric_storage;
  std::vector<astl_metric_handle_t> active_metrics{active_metric_handle};
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetAvailableMetrics(target_active_ptr))
                             .RETURN(std::span<const astl_metric_handle_t>(active_metrics)));
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, GetAvailableMetrics(target_stopped_ptr))
                             .RETURN(std::span<const astl_metric_handle_t>{}));
  expectations.push_back(
      NAMED_ALLOW_CALL(*mm_raw, GetRequiredOperations(_, target_active_ptr))
          .RETURN(astl::CollectionOperations{
              {}, {}, {}, {}, std::chrono::milliseconds{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}));
  expectations.push_back(
      NAMED_ALLOW_CALL(*cm_raw, ConfigureCollectionOnTarget(target_active_ptr, _, _)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(
      NAMED_ALLOW_CALL(*cm_raw, GetNativeClockSnapshot(target_active_ptr))
          .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}}));
  expectations.push_back(NAMED_ALLOW_CALL(*mm_raw, SetClockCorrelations(_)));
  expectations.push_back(NAMED_ALLOW_CALL(*cm_raw, StartOnTarget(target_active_ptr)).RETURN(ASTL_STATUS_SUCCESS));

  const astl_collection_params_t      collection_params{.size  = sizeof(astl_collection_params_t),
                                                        .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
                                                        .sampling_interval = 0,
                                                        .collection_mode   = ASTL_COLLECTION_MODE_SAMPLING};
  std::array<astl_metric_handle_t, 1> active_metrics_span{active_metric_handle};
  REQUIRE(orch_raw->ConfigureMetricCollection(target_active_ptr, &collection_params, active_metrics_span) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(orch_raw->StartCollection(target_active_ptr) == ASTL_STATUS_SUCCESS);

  TestOrchestratorInjector injector(std::move(orchestrator));

  // CropSamples must fail without touching target_stopped's samples
  REQUIRE(CropSamples(200, 400) == ASTL_STATUS_COLLECTION_NOT_STOPPED);

  uint32_t count = 99;
  REQUIRE(GetMetricSampleCountOnTarget(target_stopped_hdl, metric_hdl_stopped, &count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 5);
}
