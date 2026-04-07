// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file pause_marker_utests.cpp
 * @brief Tests for pause-marker propagation through the MetricManager pipeline.
 *
 * These tests exercise the full signal path:
 *   RawSampledData (with PauseMarker sentinels)
 *     → MetricManager::ProcessRawSamples
 *       → concrete metric (DeltaMetric / RateMetric / ResidencyMetric)
 *         → processed-sample output captured by CapturingSink
 *
 * The intent is to verify that:
 *   1. Pause markers are routed to every metric registered for the target.
 *   2. Each metric type resets its state correctly on pause so that no cross-pause
 *      delta/rate/residency is emitted.
 *   3. Normal processing resumes correctly after the pause with a fresh baseline.
 */

#include <chrono>
#include <memory>
#include <span>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // must precede Catch2 headers
#include "common/capabilities.hpp"
#include "common/clock_correlation.hpp"
#include "common/metric_config.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "metric/metric_manager.hpp"

using namespace std::chrono_literals;

// ──────────────────────────────────────────────────────────────────────────────
// Test helpers
// ──────────────────────────────────────────────────────────────────────────────

/**
 * @brief Sink that captures processed samples, allowing integration tests to assert on the
 *        sample stream produced by metric types under test.
 */
struct CapturingSink : public astl::IProcessedSampleSink {
  std::vector<astl::ProcessedSampledData> samples;

  astl_status_code SinkProcessedSamples(const astl::ITarget* /*target*/, const astl::IMetric* /*metric*/,
                                        std::span<const astl::ProcessedSampledData> incoming) override {
    samples.insert(samples.end(), incoming.begin(), incoming.end());
    return ASTL_STATUS_SUCCESS;
  }
};

/** Build a Capabilities object that supports one collector type. */
static astl::Capabilities MakeCaps(astl::CollectorType type = astl::CollectorType::SCMI) {
  return astl::Capabilities{{astl::CollectorCapability{type}}, {astl::SystemCapability{}}};
}

/**
 * @brief Build a zero-baseline ClockCorrelationMap for a set of operation IDs.
 *
 * With native_at_start = 0 and raw_at_start = 0 the normalization formula reduces to:
 *   normalized_ts = raw_tick * tick_ratio
 *
 * Using MakeTickRatio<SampleMicroseconds>() means 1 raw_tick == 1 microsecond expressed as
 * nanoseconds in the ProcessedSampleTimestamp domain, so raw_tick=1'000'000 → 1 s.
 */
static astl::ClockCorrelationMap MakeZeroCorrelationMap(std::initializer_list<astl::OperationId> op_ids) {
  astl::ClockCorrelationMap corr;
  for (auto op_id : op_ids) {
    corr[op_id] = astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{0}},
                                                  uint64_t{0}, astl::MakeTickRatio<astl::SampleMicroseconds>()};
  }
  return corr;
}

// ──────────────────────────────────────────────────────────────────────────────
// DeltaMetric integration tests
// ──────────────────────────────────────────────────────────────────────────────

/**
 * Helper that sets up a MetricManager with one DeltaMetric registered for @p target,
 * populates the operation routing and clock correlations, and returns the operation
 * ID discovered from GetRequiredOperations().
 *
 * @param mgr        MetricManager instance (already constructed with matching caps).
 * @param target     Target to register the metric against.
 * @param sink       Capturing sink to register for output observation.
 * @return           The single operation ID assigned to the delta metric.
 */
static astl::OperationId SetupDeltaMetric(astl::MetricManager& mgr, const astl::ITarget* target, CapturingSink& sink) {
  auto cfg = std::make_unique<astl::MetricConfig>("energy_delta", "Accumulated energy delta", ASTL_UNITS_JOULES,
                                                  ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_DELTA,
                                                  astl::CollectorType::SCMI, astl::ScmiOperationBuilder{0xD001});

  REQUIRE(mgr.RegisterMetric(std::move(cfg), {target}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);

  auto avail = mgr.GetAvailableMetrics(target);
  REQUIRE(avail.has_value());
  auto ops = mgr.GetRequiredOperations(*avail, target);
  REQUIRE(ops.has_value());
  REQUIRE(ops->operationsOnSample.size() == 1);

  astl::OperationId op_id = ops->operationsOnSample.front()->GetId();
  mgr.SetClockCorrelations(MakeZeroCorrelationMap({op_id}));
  return op_id;
}

TEST_CASE("DeltaMetric integration: pause resets delta baseline across MetricManager pipeline",
          "[integration][DeltaMetric][pause]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"SoC"};
  CapturingSink       sink;

  const astl::OperationId op_id = SetupDeltaMetric(mgr, &target, sink);

  // ── Pre-pause samples ──────────────────────────────────────────────────────
  // t=0s  (tick=0)       → v=100  (baseline, no delta)
  // t=1s  (tick=1000000) → v=250  → delta=150
  astl::RawSamplesMap pre_pause;
  pre_pause[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{100}}, 0ULL        },
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{250}}, 1'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(pre_pause) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples[0].get<uint64_t>() == 150);

  // ── Pause marker ──────────────────────────────────────────────────────────
  // Pause at t=1.5s → MetricManager routes it to DeltaMetric, which clears _previous_sample.
  const auto          pause_ts = astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1'500'000'000}};
  astl::RawSamplesMap pause_map;
  pause_map[&target] = {astl::RawSampledData::PauseMarker(pause_ts)};
  REQUIRE(mgr.ProcessRawSamples(pause_map) == ASTL_STATUS_SUCCESS);
  // No processed sample is expected from delta/rate on a pause; the pause event metric
  // (if registered) would capture it — but that is not under test here.

  // ── Post-pause samples ─────────────────────────────────────────────────────
  // t=2s (tick=2000000) → v=300  → new baseline (no delta emitted)
  // t=3s (tick=3000000) → v=440  → delta=140
  astl::RawSamplesMap post_pause;
  post_pause[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{300}}, 2'000'000ULL},
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{440}}, 3'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(post_pause) == ASTL_STATUS_SUCCESS);

  // Total: delta=150 (before pause) + delta=140 (after pause) — NOT a delta across the gap
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<uint64_t>() == 150);
  CHECK(sink.samples[1].get<uint64_t>() == 140);
}

TEST_CASE("DeltaMetric integration: multiple pauses each reset the delta baseline independently",
          "[integration][DeltaMetric][pause]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"SoC"};
  CapturingSink       sink;

  const astl::OperationId op_id = SetupDeltaMetric(mgr, &target, sink);

  // First window: t=0 v=0 (baseline), t=1s v=100 → delta=100
  astl::RawSamplesMap win1;
  win1[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{0}},   0ULL        },
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{100}}, 1'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(win1) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);

  // Pause 1 at t=1.5s
  astl::RawSamplesMap pause1;
  pause1[&target] = {
      astl::RawSampledData::PauseMarker(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1'500'000'000}})};
  REQUIRE(mgr.ProcessRawSamples(pause1) == ASTL_STATUS_SUCCESS);

  // Second window: t=2s v=200 (new baseline), t=3s v=350 → delta=150
  astl::RawSamplesMap win2;
  win2[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{200}}, 2'000'000ULL},
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{350}}, 3'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(win2) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);

  // Pause 2 at t=3.5s
  astl::RawSamplesMap pause2;
  pause2[&target] = {
      astl::RawSampledData::PauseMarker(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{3'500'000'000}})};
  REQUIRE(mgr.ProcessRawSamples(pause2) == ASTL_STATUS_SUCCESS);

  // Third window: t=4s v=500 (new baseline), t=5s v=600 → delta=100
  astl::RawSamplesMap win3;
  win3[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{500}}, 4'000'000ULL},
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{600}}, 5'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(win3) == ASTL_STATUS_SUCCESS);

  REQUIRE(sink.samples.size() == 3);
  CHECK(sink.samples[0].get<uint64_t>() == 100);
  CHECK(sink.samples[1].get<uint64_t>() == 150);
  CHECK(sink.samples[2].get<uint64_t>() == 100);
}

// ──────────────────────────────────────────────────────────────────────────────
// RateMetric integration tests
// ──────────────────────────────────────────────────────────────────────────────

/** Helper that registers a RateMetric; returns the op_id. */
static astl::OperationId SetupRateMetric(astl::MetricManager& mgr, const astl::ITarget* target, CapturingSink& sink) {
  auto cfg = std::make_unique<astl::MetricConfig>("power_rate", "Power rate", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64,
                                                  ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_RATE,
                                                  astl::CollectorType::SCMI, astl::ScmiOperationBuilder{0xD002});

  REQUIRE(mgr.RegisterMetric(std::move(cfg), {target}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);

  auto avail = mgr.GetAvailableMetrics(target);
  REQUIRE(avail.has_value());
  auto ops = mgr.GetRequiredOperations(*avail, target);
  REQUIRE(ops.has_value());
  REQUIRE(ops->operationsOnSample.size() == 1);

  astl::OperationId op_id = ops->operationsOnSample.front()->GetId();
  mgr.SetClockCorrelations(MakeZeroCorrelationMap({op_id}));
  return op_id;
}

TEST_CASE("RateMetric integration: pause resets rate baseline across MetricManager pipeline",
          "[integration][RateMetric][pause]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"SoC"};
  CapturingSink       sink;

  const astl::OperationId op_id = SetupRateMetric(mgr, &target, sink);

  // ── Pre-pause window ───────────────────────────────────────────────────────
  // t=0s  v=0      (baseline)
  // t=1s  v=2000   delta=2000 over 1s → rate = 2000 J/s
  // (RateMetric: rate = delta_double / interval_us * 1e6; interval_us = 1e6 → rate = 2000.0)
  astl::RawSamplesMap pre_pause;
  pre_pause[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{0}},    0ULL        },
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{2000}}, 1'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(pre_pause) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(std::get<double>(sink.samples[0].value.value) == Catch::Approx(2000.0));

  // -- Pause at t=1.5s
  // RateMetric inherits DeltaMetric::ProcessPauseSample; clears _previous_sample.
  const auto          pause_ts = astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1'500'000'000}};
  astl::RawSamplesMap pause_map;
  pause_map[&target] = {astl::RawSampledData::PauseMarker(pause_ts)};
  REQUIRE(mgr.ProcessRawSamples(pause_map) == ASTL_STATUS_SUCCESS);

  // ── Post-pause window ──────────────────────────────────────────────────────
  // t=3s  v=3000   → new baseline (no rate emitted)
  // t=4s  v=4000   delta=1000 over 1s → rate = 1000 J/s
  astl::RawSamplesMap post_pause;
  post_pause[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{3000}}, 3'000'000ULL},
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{4000}}, 4'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(post_pause) == ASTL_STATUS_SUCCESS);

  REQUIRE(sink.samples.size() == 2);
  CHECK(std::get<double>(sink.samples[0].value.value) == Catch::Approx(2000.0));
  CHECK(std::get<double>(sink.samples[1].value.value) == Catch::Approx(1000.0));
}

TEST_CASE("RateMetric integration: pause at start of collection acts like no-op on empty baseline",
          "[integration][RateMetric][pause]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"SoC"};
  CapturingSink       sink;

  const astl::OperationId op_id = SetupRateMetric(mgr, &target, sink);

  // Send pause before any samples — should not crash
  astl::RawSamplesMap pause_map;
  pause_map[&target] = {
      astl::RawSampledData::PauseMarker(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{500'000'000}})};
  REQUIRE(mgr.ProcessRawSamples(pause_map) == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());

  // After pause, normal samples should still produce rates
  astl::RawSamplesMap post;
  post[&target] = {
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{100}}, 1'000'000ULL},
      astl::RawSampledData{op_id, astl::AstlValue{uint64_t{300}}, 2'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(post) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(std::get<double>(sink.samples[0].value.value) == Catch::Approx(200.0));
}

// ──────────────────────────────────────────────────────────────────────────────
// ResidencyMetric integration tests
// ──────────────────────────────────────────────────────────────────────────────

static const astl::ResidencyMetricConfig::StateToInfoMap& GetResidencyStateMap() {
  static const astl::ResidencyMetricConfig::StateToInfoMap state_map{
      {"C6", {"C6", "CPU deep sleep", 1'000'000.0, astl::ScmiOperationBuilder{0x67DE}}},
      {"C1", {"C1", "CPU clock gate", 1'000'000.0, astl::ScmiOperationBuilder{0x68DE}}},
  };
  return state_map;
}

/**
 * @brief Helper: register a ResidencyMetric, capture op_ids for the two states.
 * @return  Vector of op_ids in the order returned by GetRequiredOperations.
 */
static std::vector<astl::OperationId> SetupResidencyMetric(astl::MetricManager& mgr, const astl::ITarget* target,
                                                           CapturingSink& sink) {
  auto cfg = std::make_unique<astl::ResidencyMetricConfig>(
      "cpu_cstate_residency", "CPU C-state residency", ASTL_UNITS_TICKS, ASTL_VALUE_UINT64, ASTL_METRIC_RESIDENCY,
      ASTL_METRIC_IDENTIFIER_UNKNOWN, astl::CollectorType::SCMI, GetResidencyStateMap(),
      astl::ResidencyMetricConfig::InferredStateInfo{"Active", "CPU active state"});

  REQUIRE(mgr.RegisterMetric(std::move(cfg), {target}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);

  auto avail = mgr.GetAvailableMetrics(target);
  REQUIRE(avail.has_value());
  auto ops = mgr.GetRequiredOperations(*avail, target);
  REQUIRE(ops.has_value());
  // 2 states → 2 operations
  REQUIRE(ops->operationsOnSample.size() == 2);

  std::vector<astl::OperationId> op_ids;
  astl::ClockCorrelationMap      corr_map;
  for (const auto& operation : ops->operationsOnSample) {
    auto operation_id = operation->GetId();
    op_ids.push_back(operation_id);
    corr_map[operation_id] =
        astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{0}}, uint64_t{0},
                                        astl::MakeTickRatio<astl::SampleMicroseconds>()};
  }
  mgr.SetClockCorrelations(corr_map);
  return op_ids;
}

TEST_CASE("ResidencyMetric integration: pause resets state baseline across MetricManager pipeline",
          "[integration][ResidencyMetric][pause]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"SoC"};
  CapturingSink       sink;

  const auto op_ids = SetupResidencyMetric(mgr, &target, sink);
  REQUIRE(op_ids.size() == 2);
  const auto op_c6 = op_ids[0];
  const auto op_c1 = op_ids[1];

  // ── Baseline readings at t=0 ───────────────────────────────────────────────
  // Each state operation sends its initial tick count.  ResidencyMetric stores
  // these as _previous_samples — no processed output yet.
  // tick=0 → normalized_ts = 0 ns
  astl::RawSamplesMap baseline;
  baseline[&target] = {
      astl::RawSampledData{op_c6, astl::AstlValue{uint64_t{10'000}}, 0ULL},
      astl::RawSampledData{op_c1, astl::AstlValue{uint64_t{5'000}},  0ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(baseline) == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());

  // ── First interval readings at t=1s ────────────────────────────────────────
  // tick=1'000'000 → normalized_ts = 1 s
  // C6 ticks: 10000→11000 (+1000), C1 ticks: 5000→5500 (+500)
  // Residency percentages are computed over the 1s interval for each state + inferred Active.
  astl::RawSamplesMap interval1;
  interval1[&target] = {
      astl::RawSampledData{op_c6, astl::AstlValue{uint64_t{11'000}}, 1'000'000ULL},
      astl::RawSampledData{op_c1, astl::AstlValue{uint64_t{5'500}},  1'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(interval1) == ASTL_STATUS_SUCCESS);
  // ResidencyMetric emits one processed sample per state (including inferred Active).
  // We only verify that samples were emitted — the exact percentages are covered by
  // residency_metric_utests.cpp.
  CHECK(!sink.samples.empty());

  const auto samples_before_pause = sink.samples.size();

  // -- Pause at t=1.5s
  // ResidencyMetric::ProcessPauseSample clears _previous_samples and all pending state.
  const auto          pause_ts = astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1'500'000'000}};
  astl::RawSamplesMap pause_map;
  pause_map[&target] = {astl::RawSampledData::PauseMarker(pause_ts)};
  REQUIRE(mgr.ProcessRawSamples(pause_map) == ASTL_STATUS_SUCCESS);

  // ── First post-pause readings at t=2s ─────────────────────────────────────
  // These establish the new baseline; no residency output is expected because
  // _previous_samples was cleared by the pause.
  // tick=2'000'000 → normalized_ts = 2 s
  astl::RawSamplesMap post_pause_baseline;
  post_pause_baseline[&target] = {
      astl::RawSampledData{op_c6, astl::AstlValue{uint64_t{20'000}}, 2'000'000ULL},
      astl::RawSampledData{op_c1, astl::AstlValue{uint64_t{8'000}},  2'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(post_pause_baseline) == ASTL_STATUS_SUCCESS);
  // No new samples should have been emitted — the readings above are a fresh baseline
  CHECK(sink.samples.size() == samples_before_pause);

  // ── Second post-pause readings at t=3s ────────────────────────────────────
  // Now a full interval [2s, 3s] is available — residency is emitted again.
  astl::RawSamplesMap post_pause_interval;
  post_pause_interval[&target] = {
      astl::RawSampledData{op_c6, astl::AstlValue{uint64_t{21'000}}, 3'000'000ULL},
      astl::RawSampledData{op_c1, astl::AstlValue{uint64_t{8'500}},  3'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(post_pause_interval) == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.size() > samples_before_pause);
}

TEST_CASE("ResidencyMetric integration: pause marker forwarded even with no prior state readings",
          "[integration][ResidencyMetric][pause]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"SoC"};
  CapturingSink       sink;

  const auto op_ids = SetupResidencyMetric(mgr, &target, sink);
  REQUIRE(op_ids.size() == 2);

  // Send a pause before any state readings have been received.
  astl::RawSamplesMap pause_map;
  pause_map[&target] = {
      astl::RawSampledData::PauseMarker(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{500'000'000}})};
  REQUIRE(mgr.ProcessRawSamples(pause_map) == ASTL_STATUS_SUCCESS);

  // No processed residency sample is expected; the pause event metric (if registered)
  // would capture the pause — but that is not under test here.
  CHECK(sink.samples.empty());

  // Collection should recover normally: baseline then interval.
  const auto op_c6 = op_ids[0];
  const auto op_c1 = op_ids[1];

  astl::RawSamplesMap baseline;
  baseline[&target] = {
      astl::RawSampledData{op_c6, astl::AstlValue{uint64_t{0}}, 1'000'000ULL},
      astl::RawSampledData{op_c1, astl::AstlValue{uint64_t{0}}, 1'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(baseline) == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());  // still just a baseline

  astl::RawSamplesMap interval;
  interval[&target] = {
      astl::RawSampledData{op_c6, astl::AstlValue{uint64_t{500}},  2'000'000ULL},
      astl::RawSampledData{op_c1, astl::AstlValue{uint64_t{1000}}, 2'000'000ULL},
  };
  REQUIRE(mgr.ProcessRawSamples(interval) == ASTL_STATUS_SUCCESS);
  CHECK(!sink.samples.empty());  // residency emitted for the [1s, 2s] window
}
