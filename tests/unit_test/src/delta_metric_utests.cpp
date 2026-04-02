// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <limits>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "metric/delta_metric.hpp"
#include "operation/operation_builder.hpp"

// some helpers to cut down duplicated code initializing metrics and their configuration
static astl::MetricConfig* GetDeltaConfig() {
  static astl::MetricConfig config{"test_metric",
                                   "unit-test metric",
                                   ASTL_UNITS_CELSIUS,
                                   ASTL_VALUE_UINT64,
                                   ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                   ASTL_METRIC_DELTA,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return &config;
}

static astl::MetricConfig* GetDeltaConfigUINT32() {
  static astl::MetricConfig config{"test_metric",
                                   "unit-test metric",
                                   ASTL_UNITS_CELSIUS,
                                   ASTL_VALUE_UINT32,
                                   ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                   ASTL_METRIC_DELTA,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return &config;
}

static astl::MetricConfig* GetDeltaConfigFLOAT64() {
  static astl::MetricConfig config{"test_metric",
                                   "unit-test metric",
                                   ASTL_UNITS_CELSIUS,
                                   ASTL_VALUE_FLOAT64,
                                   ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                   ASTL_METRIC_DELTA,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return &config;
}

// Test fixture class to access protected members
class DeltaMetricTestFixture : public astl::DeltaMetric {
 public:
  DeltaMetricTestFixture() : astl::DeltaMetric(GetDeltaConfig(), nullptr, nullptr) {}

  // Expose protected methods for testing
  static std::expected<astl::AstlValue, astl_status_code> TestCalculateDelta(const astl::AstlValue& current_sample,
                                                                             const astl::AstlValue& previous_sample) {
    return CalculateDelta(current_sample, previous_sample);
  }
};

TEST_CASE("DeltaMetric: construction", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};
  auto              summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());
  REQUIRE(sink.captured.empty());
}

TEST_CASE("DeltaMetric: single sample - no delta calculated", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};
  // First sample should not produce a delta
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1);
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Verify no delta was calculated
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  REQUIRE(sink.captured.empty());
}

TEST_CASE("DeltaMetric: two samples - delta calculated", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};

  // First sample
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1);
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Second sample - should produce delta
  astl::AstlValue             val2{uint64_t{150}};
  astl::NormalizedSampledData sample2(2, val2);
  auto                        status2 = metric.ReceiveRawSample(sample2);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);

  // Verify delta was calculated (150 - 100 = 50)
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(summary.min_delta.has_value());
  REQUIRE(summary.max_delta.has_value());

  REQUIRE(sink.captured.size() == 1);
  auto delta_value = std::get<uint64_t>(sink.captured[0].value.value);
  REQUIRE(delta_value == 50);
}

TEST_CASE("DeltaMetric: multiple samples - deltas calculated", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};
  // Feed multiple samples with known deltas
  std::vector<uint64_t> values = {100, 150, 170, 180, 200};
  // Expected deltas: 50, 20, 10, 20
  std::vector<uint64_t> expected_deltas = {50, 20, 10, 20};

  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue             sample_value{uint64_t{values[i]}};
    astl::NormalizedSampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto                        status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Verify deltas were calculated correctly
  REQUIRE(sink.captured.size() == 4);

  // Check each delta value
  for (size_t i = 0; i < expected_deltas.size(); ++i) {
    auto delta_value = std::get<uint64_t>(sink.captured[i].value.value);
    REQUIRE(delta_value == expected_deltas[i]);
  }
}

TEST_CASE("DeltaMetric: invalid sample type", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfigUINT32(), nullptr, &sink};
  // Try to feed a UINT64 sample
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1);
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);

  // Verify no delta was calculated
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  REQUIRE(sink.captured.empty());
}

TEST_CASE("DeltaMetric: static CalculateDelta function", "[DeltaMetric]") {
  SECTION("Valid arithmetic values") {
    astl::AstlValue current{uint64_t{150}};
    astl::AstlValue previous{uint64_t{100}};

    auto result = DeltaMetricTestFixture::TestCalculateDelta(current, previous);
    REQUIRE(result.has_value());

    auto delta_value = std::get<uint64_t>(result.value().value);
    REQUIRE(delta_value == 50);
  }
}

TEST_CASE("DeltaMetric: different value types", "[DeltaMetric]") {
  SECTION("UINT32 values") {
    MockSampleSink    sink;
    astl::DeltaMetric metric{GetDeltaConfigUINT32(), nullptr, &sink};

    astl::AstlValue             val1{uint32_t{100}};
    astl::AstlValue             val2{uint32_t{150}};
    astl::NormalizedSampledData sample1(1, val1);
    astl::NormalizedSampledData sample2(2, val2);

    auto status1 = metric.ReceiveRawSample(sample1);
    auto status2 = metric.ReceiveRawSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    // Verify delta was captured in sink
    REQUIRE(sink.captured.size() == 1);
    auto delta_value = std::get<uint32_t>(sink.captured[0].value.value);
    REQUIRE(delta_value == 50);
  }

  SECTION("Large UINT64 values") {
    MockSampleSink              sink;
    astl::DeltaMetric           metric{GetDeltaConfig(), nullptr, &sink};
    astl::AstlValue             val1{uint64_t{100}};
    astl::AstlValue             val2{uint64_t{150}};
    astl::NormalizedSampledData sample1(1, val1);
    astl::NormalizedSampledData sample2(2, val2);

    auto status1 = metric.ReceiveRawSample(sample1);
    auto status2 = metric.ReceiveRawSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    REQUIRE(sink.captured.size() == 1);
    auto delta_value = std::get<uint64_t>(sink.captured[0].value.value);
    REQUIRE(delta_value == 50);
  }

  SECTION("FLOAT64 values") {
    MockSampleSink sink;
    // Use the shared FLOAT64 configuration helper
    astl::DeltaMetric           metric{GetDeltaConfigFLOAT64(), nullptr, &sink};
    astl::AstlValue             val1{100.5};
    astl::AstlValue             val2{150.5};
    astl::NormalizedSampledData sample1(1, val1);
    astl::NormalizedSampledData sample2(2, val2);

    auto status1 = metric.ReceiveRawSample(sample1);
    auto status2 = metric.ReceiveRawSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    REQUIRE(sink.captured.size() == 1);
    auto delta_value = std::get<double>(sink.captured[0].value.value);
    REQUIRE(delta_value == 50.0);
  }
}

TEST_CASE("DeltaMetric: edge cases", "[DeltaMetric]") {
  SECTION("Zero delta") {
    MockSampleSink    sink;
    astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};

    astl::AstlValue             val1{uint64_t{100}};
    astl::AstlValue             val2{uint64_t{100}};
    astl::NormalizedSampledData sample1(1, val1);
    astl::NormalizedSampledData sample2(2, val2);

    auto status1 = metric.ReceiveRawSample(sample1);
    auto status2 = metric.ReceiveRawSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    REQUIRE(sink.captured.size() == 1);
    auto delta_value = std::get<uint64_t>(sink.captured[0].value.value);
    REQUIRE(delta_value == 0);
  }

  SECTION("Large delta values") {
    MockSampleSink              sink;
    astl::DeltaMetric           metric{GetDeltaConfig(), nullptr, &sink};
    astl::AstlValue             val1{uint64_t{0}};
    astl::AstlValue             val2{std::numeric_limits<uint64_t>::max()};
    astl::NormalizedSampledData sample1(1, val1);
    astl::NormalizedSampledData sample2(2, val2);

    auto status1 = metric.ReceiveRawSample(sample1);
    auto status2 = metric.ReceiveRawSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    REQUIRE(sink.captured.size() == 1);
    auto delta_value = std::get<uint64_t>(sink.captured[0].value.value);
    REQUIRE(delta_value == std::numeric_limits<uint64_t>::max());
  }
}

TEST_CASE("DeltaMetric: summarize calculates statistics", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};

  // Feed samples with known deltas
  std::vector<uint64_t> values = {100, 110, 120, 130};
  // Expected deltas: 10, 10, 10 (all positive, same value)

  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue             sample_value{uint64_t{values[i]}};
    astl::NormalizedSampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto                        status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Summarize to calculate statistics
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  // Verify statistics
  auto summary   = metric.GetDeltaSummaryData();
  auto min_delta = std::get<uint64_t>(summary.min_delta.value().value);
  auto max_delta = std::get<uint64_t>(summary.max_delta.value().value);
  auto avg_delta = std::get<uint64_t>(summary.avg_delta.value().value);

  REQUIRE(min_delta == 10);
  REQUIRE(max_delta == 10);
  REQUIRE(avg_delta == 10);
}

TEST_CASE("DeltaMetric: min/max statistics for datatype : double", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfigFLOAT64(), nullptr, &sink};

  // Feed samples with varying deltas
  std::vector<double> values = {100.0, 110.0, 105.0, 125.0, 120.0};
  // Expected deltas: 10, -5, 20, -5

  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue             sample_value{double{values[i]}};
    astl::NormalizedSampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto                        status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Summarize to calculate statistics
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(sink.captured.size() == 4);

  // With signed arithmetic, we expect min to be -5 and max to be 20
  auto min_delta = std::get<double>(summary.min_delta.value().value);
  auto max_delta = std::get<double>(summary.max_delta.value().value);

  REQUIRE(min_delta == -5.0);
  REQUIRE(max_delta == 20.0);
}

TEST_CASE("DeltaMetric: no samples summarize", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};
  // Summarize without any samples
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  REQUIRE(sink.captured.empty());
}

TEST_CASE("DeltaMetric: Reset functionality", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};
  // Feed some samples to create state
  std::vector<uint64_t> values = {100, 150, 200};
  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue             sample_value{uint64_t{values[i]}};
    astl::NormalizedSampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto                        status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Verify state exists
  REQUIRE(sink.captured.size() == 2);

  // Reset the metric
  metric.Reset();
  // After reset summary statistics should be cleared
  auto summary_after_reset = metric.GetDeltaSummaryData();
  REQUIRE(!summary_after_reset.min_delta.has_value());
  REQUIRE(!summary_after_reset.max_delta.has_value());
  auto avg = std::get<uint64_t>(summary_after_reset.avg_delta.value().value);
  REQUIRE(avg == 0);
}

TEST_CASE("DeltaMetric: Delta processing with sink", "[DeltaMetric]") {
  MockSampleSink    sink;
  astl::DeltaMetric metric{GetDeltaConfig(), nullptr, &sink};

  // Test when no samples have been received
  REQUIRE(sink.captured.empty());

  // Add some samples
  astl::AstlValue             val1{uint64_t{100}};
  astl::AstlValue             val2{uint64_t{150}};
  astl::NormalizedSampledData sample1(1, val1);
  astl::NormalizedSampledData sample2(2, val2);

  auto status1 = metric.ReceiveRawSample(sample1);
  auto status2 = metric.ReceiveRawSample(sample2);

  REQUIRE(status1 == ASTL_STATUS_SUCCESS);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);

  // DeltaMetric should have processed the delta and sent it to the sink
  REQUIRE(sink.captured.size() == 1);
  auto delta_value = std::get<uint64_t>(sink.captured[0].value.value);
  REQUIRE(delta_value == 50);
}
