// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file lifecycle_events_utests.cpp
 * @brief Unit tests for lifecycle event injection via MetricManager::InjectLifecycleEvent.
 *
 * Verifies that:
 *   1. CROP_BEGIN / CROP_END events can be injected into the lifecycle metric
 *      and appear in the processed sample stream with the correct uint64 event values.
 *   2. PAUSE (0) and RESUME (1) can be similarly injected, confirming the event type values
 *      match the public enum constants.
 *   3. InjectLifecycleEvent is a no-op (returns SUCCESS) when no lifecycle metric is registered
 *      for the target.
 *   4. InjectLifecycleEvent returns BAD_ARGUMENT on a null target.
 *   5. The astl_lifecycle_event_type_t enum values match the documented API contract.
 */

#include <chrono>
#include <memory>
#include <span>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "astl/astl_telemetry.h"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "metric/metric_manager.hpp"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

struct CapturingSink : public astl::IProcessedSampleSink {
  std::vector<astl::ProcessedSampledData> samples;

  astl_status_code SinkProcessedSamples(const astl::ITarget* /*target*/, const astl::IMetric* /*metric*/,
                                        std::span<const astl::ProcessedSampledData> incoming) override {
    samples.insert(samples.end(), incoming.begin(), incoming.end());
    return ASTL_STATUS_SUCCESS;
  }
};

static astl::Capabilities MakeCaps() {
  return astl::Capabilities{{astl::CollectorCapability{astl::CollectorType::SCMI}}, {astl::SystemCapability{}}};
}

/** Register the synthetic astl_lifecycle_events.<target> metric, mirroring how Orchestrator does it. */
static void RegisterLifecycleMetric(astl::MetricManager& mgr, const astl::ITarget* target) {
  const std::string metric_name = std::string{"astl_lifecycle_events."} + target->Name();
  auto cfg = std::make_unique<astl::MetricConfig>(metric_name, "ASTL lifecycle events (pause, resume, crop boundary)",
                                                  ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                                  ASTL_METRIC_EVENT, astl::CollectorType::ASTL_NATIVE,
                                                  astl::NullOperationBuilder{});
  REQUIRE(mgr.RegisterMetric(std::move(cfg), {target}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.GetLifecycleEventMetricOnTarget(target) != nullptr);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("InjectLifecycleEvent: no-op when no lifecycle metric registered for target", "[MetricManager][lifecycle]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"unregistered-target"};

  REQUIRE(mgr.InjectLifecycleEvent(&target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN),
                                   astl::ClockMonotonicRaw::now()) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("InjectLifecycleEvent: returns BAD_ARGUMENT on null target", "[MetricManager][lifecycle]") {
  astl::MetricManager mgr{MakeCaps()};
  REQUIRE(mgr.InjectLifecycleEvent(nullptr, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN),
                                   astl::ClockMonotonicRaw::now()) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("InjectLifecycleEvent: GetLifecycleEventMetricOnTarget returns non-null after registration",
          "[MetricManager][lifecycle]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"lookup-target"};

  REQUIRE(mgr.GetLifecycleEventMetricOnTarget(&target) == nullptr);
  RegisterLifecycleMetric(mgr, &target);
  REQUIRE(mgr.GetLifecycleEventMetricOnTarget(&target) != nullptr);
}

TEST_CASE("InjectLifecycleEvent: CROP_BEGIN and CROP_END events carry exact timestamps", "[MetricManager][lifecycle]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"crop-target"};
  CapturingSink       sink;
  REQUIRE(mgr.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);
  RegisterLifecycleMetric(mgr, &target);

  const astl::ProcessedSampleTimestamp ts_begin{std::chrono::duration<int64_t, std::nano>{int64_t{1'000'000'000}}};
  const astl::ProcessedSampleTimestamp ts_end{std::chrono::duration<int64_t, std::nano>{int64_t{2'000'000'000}}};

  REQUIRE(mgr.InjectLifecycleEvent(&target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN), ts_begin) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.InjectLifecycleEvent(&target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_END), ts_end) ==
          ASTL_STATUS_SUCCESS);

  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<uint64_t>() == static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN));
  CHECK(sink.samples[1].get<uint64_t>() == static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_END));
  CHECK(sink.samples[0].timestamp == ts_begin);
  CHECK(sink.samples[1].timestamp == ts_end);
}

TEST_CASE("InjectLifecycleEvent: PAUSE and RESUME event values match enum constants", "[MetricManager][lifecycle]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target{"pause-resume-target"};
  CapturingSink       sink;
  REQUIRE(mgr.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);
  RegisterLifecycleMetric(mgr, &target);

  const auto timestamp = astl::ClockMonotonicRaw::now();
  REQUIRE(mgr.InjectLifecycleEvent(&target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_PAUSE), timestamp) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.InjectLifecycleEvent(&target, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_RESUME), timestamp) ==
          ASTL_STATUS_SUCCESS);

  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<uint64_t>() == uint64_t{0});
  CHECK(sink.samples[1].get<uint64_t>() == uint64_t{1});
}

TEST_CASE("InjectLifecycleEvent: independent per-target isolation — events not cross-contaminated",
          "[MetricManager][lifecycle]") {
  astl::MetricManager mgr{MakeCaps()};
  TestTargetBase      target_a{"target-a"};
  TestTargetBase      target_b{"target-b"};
  CapturingSink       sink;
  REQUIRE(mgr.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);
  RegisterLifecycleMetric(mgr, &target_a);
  RegisterLifecycleMetric(mgr, &target_b);

  const auto timestamp = astl::ClockMonotonicRaw::now();
  REQUIRE(mgr.InjectLifecycleEvent(&target_a, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN), timestamp) ==
          ASTL_STATUS_SUCCESS);

  // target_b should not be injected yet — verify by checking that only 1 sample exists overall.
  REQUIRE(sink.samples.size() == 1);
  CHECK(sink.samples[0].get<uint64_t>() == static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_BEGIN));

  REQUIRE(mgr.InjectLifecycleEvent(&target_b, static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_END), timestamp) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[1].get<uint64_t>() == static_cast<uint64_t>(ASTL_LIFECYCLE_EVENT_CROP_END));
}

TEST_CASE("astl_lifecycle_event_type_t enum values match the documented API contract", "[lifecycle][api]") {
  // Compile-time guard: these values are part of the stable public API.
  static_assert(ASTL_LIFECYCLE_EVENT_PAUSE == 0, "PAUSE must be 0");
  static_assert(ASTL_LIFECYCLE_EVENT_RESUME == 1, "RESUME must be 1");
  static_assert(ASTL_LIFECYCLE_EVENT_CROP_BEGIN == 2, "CROP_BEGIN must be 2");
  static_assert(ASTL_LIFECYCLE_EVENT_CROP_END == 3, "CROP_END must be 3");
  SUCCEED("enum values are correct");
}
