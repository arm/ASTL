// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <expected>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "metric/rate_metric.hpp"

static const astl::MetricConfig* GetRateConfig() {
  static astl::MetricConfig config{"test_rate",
                                   "unit-test rate metric",
                                   ASTL_UNITS_JOULES,
                                   ASTL_VALUE_UINT64,
                                   ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                   ASTL_METRIC_RATE,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return &config;
}

static astl::RateMetric GetRateMetricWithSink(MockSampleSink* sink) {
  return astl::RateMetric{GetRateConfig(), nullptr, sink};
}

// Test fixture class to access protected members
class RateMetricTestFixture : public astl::RateMetric {
 public:
  RateMetricTestFixture() : astl::RateMetric(GetRateConfig(), nullptr, nullptr) {}

  // Expose protected methods for testing
  static std::expected<astl::AstlValue, astl_status_code> TestCalculateRate(const astl::AstlValue&    delta_value,
                                                                            std::chrono::microseconds time_interval) {
    return CalculateRate(delta_value, time_interval);
  }
};

TEST_CASE("RateMetric: construction", "[RateMetric]") {
  // Test basic construction
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  // Verify initial state
  auto summary = metric.GetRateSummaryData();
  REQUIRE(!summary.min_rate.has_value());
  REQUIRE(!summary.max_rate.has_value());

  REQUIRE(sink.captured.empty());
}

TEST_CASE("RateMetric: single sample - no rate calculated", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  // First sample should not produce a rate
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1);
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Verify no rate was calculated
  auto summary = metric.GetRateSummaryData();
  REQUIRE(!summary.min_rate.has_value());
  REQUIRE(!summary.max_rate.has_value());

  REQUIRE(sink.captured.empty());
}

TEST_CASE("RateMetric: two samples - rate calculated", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  using nanoseconds = std::chrono::nanoseconds;

  // First sample
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1, astl::ProcessedSampleTimestamp{nanoseconds{1'000'000'000}});  // t=1s
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Second sample - should produce rate (delta=50, time=1s = 50 units/second)
  astl::AstlValue             val2{uint64_t{150}};
  astl::NormalizedSampledData sample2(2, val2, astl::ProcessedSampleTimestamp{nanoseconds{2'000'000'000}});  // t=2s
  auto                        status2 = metric.ReceiveRawSample(sample2);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);
  // Verify rate was calculated
  auto summary = metric.GetRateSummaryData();
  REQUIRE(summary.min_rate.has_value());
  REQUIRE(summary.max_rate.has_value());

  REQUIRE(sink.captured.size() == 1);
  REQUIRE(std::get<double>(sink.captured[0].value.value) == Catch::Approx(50.0));
}

TEST_CASE("RateMetric: multiple samples - rate statistics", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  using nanoseconds = std::chrono::nanoseconds;

  // Sample 1: 100 joules at t=0
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1, astl::ProcessedSampleTimestamp{nanoseconds{0}});
  metric.ReceiveRawSample(sample1);

  // Sample 2: 200 joules at t=1s (rate = 100 J/s)
  astl::AstlValue             val2{uint64_t{200}};
  astl::NormalizedSampledData sample2(2, val2, astl::ProcessedSampleTimestamp{nanoseconds{1'000'000'000}});
  metric.ReceiveRawSample(sample2);

  // Sample 3: 250 joules at t=2s (rate = 50 J/s)
  astl::AstlValue             val3{uint64_t{250}};
  astl::NormalizedSampledData sample3(3, val3, astl::ProcessedSampleTimestamp{nanoseconds{2'000'000'000}});
  metric.ReceiveRawSample(sample3);

  // Sample 4: 400 joules at t=3s (rate = 150 J/s)
  astl::AstlValue             val4{uint64_t{400}};
  astl::NormalizedSampledData sample4(4, val4, astl::ProcessedSampleTimestamp{nanoseconds{3'000'000'000}});
  metric.ReceiveRawSample(sample4);

  // Verify statistics
  REQUIRE(sink.captured.size() == 3);

  // Verify individual rates captured via sink
  REQUIRE(std::get<double>(sink.captured[0].value.value) == 100.0);  // First rate: 100.0 J/s
  REQUIRE(std::get<double>(sink.captured[1].value.value) == 50.0);   // Second rate: 50.0 J/s
  REQUIRE(std::get<double>(sink.captured[2].value.value) == 150.0);  // Third rate: 150.0 J/s

  // Test summary after calculation
  metric.Summarize();
  auto summary = metric.GetRateSummaryData();

  REQUIRE(summary.min_rate.has_value());
  REQUIRE(summary.max_rate.has_value());
  REQUIRE(summary.avg_rate.has_value());

  // Min should be 50, Max should be 150, Avg should be 100
  REQUIRE(summary.min_rate.value() == 50.0);
  REQUIRE(summary.max_rate.value() == 150.0);
  REQUIRE(summary.avg_rate.value() == Catch::Approx(100.0));
}

TEST_CASE("RateMetric: zero time interval handling", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  using nanoseconds = std::chrono::nanoseconds;

  // First sample
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1, astl::ProcessedSampleTimestamp{nanoseconds{1'000'000'000}});
  metric.ReceiveRawSample(sample1);

  // Second sample with same timestamp (zero time interval)
  astl::AstlValue             val2{uint64_t{150}};
  astl::NormalizedSampledData sample2(2, val2,
                                      astl::ProcessedSampleTimestamp{nanoseconds{1'000'000'000}});  // Same timestamp
  auto                        status = metric.ReceiveRawSample(sample2);

  // Should fail due to zero time interval
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);

  REQUIRE(sink.captured.empty());  // No rate should be calculated
}

TEST_CASE("RateMetric: invalid sample type handling", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  // Try to send a sample with wrong type (uint32 instead of uint64)
  astl::AstlValue             invalid_val{uint32_t{123}};
  astl::NormalizedSampledData invalid_sample(1, invalid_val);
  auto                        status = metric.ReceiveRawSample(invalid_sample);

  // Should fail due to type mismatch
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  REQUIRE(sink.captured.empty());
}

TEST_CASE("RateMetric: CalculateRate static method", "[RateMetric]") {
  // Test the static CalculateRate method directly
  astl::AstlValue delta{uint64_t{100}};
  auto            time_interval = std::chrono::microseconds(1000000);  // 1 second

  auto result = RateMetricTestFixture::TestCalculateRate(delta, time_interval);
  REQUIRE(result.has_value());

  // 100 units / 1 second = 100 units/second
  auto rate_value = std::get<double>(result.value().value);
  REQUIRE(rate_value == 100);
}

TEST_CASE("RateMetric: sink captures multiple rates", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  using nanoseconds = std::chrono::nanoseconds;

  // Add multiple samples to generate rates
  for (int i = 0; i < 5; ++i) {
    astl::AstlValue             val{uint64_t{100 + (static_cast<uint64_t>(i) * 50)}};
    astl::NormalizedSampledData sample(
        static_cast<uint16_t>(i + 1), val,
        astl::ProcessedSampleTimestamp{nanoseconds{static_cast<int64_t>(i) * 1'000'000'000}});
    metric.ReceiveRawSample(sample);
  }

  REQUIRE(sink.captured.size() == 4);  // 5 samples should produce 4 rates

  // Verify captured rate values are ascending sample order and positive
  int count = 0;
  for (const auto& processed : sink.captured) {
    REQUIRE(std::holds_alternative<double>(processed.value.value));
    REQUIRE(std::get<double>(processed.value.value) >= 0.0);
    count++;
  }
  REQUIRE(count == 4);
}

TEST_CASE("RateMetric: edge case - very small time intervals", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  using nanoseconds = std::chrono::nanoseconds;

  // First sample
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1, astl::ProcessedSampleTimestamp{nanoseconds{1'000'000'000}});
  metric.ReceiveRawSample(sample1);

  // Second sample with very small time difference (1 microsecond)
  astl::AstlValue             val2{uint64_t{101}};
  astl::NormalizedSampledData sample2(2, val2, astl::ProcessedSampleTimestamp{nanoseconds{1'000'001'000}});
  metric.ReceiveRawSample(sample2);

  REQUIRE(sink.captured.size() == 1);

  // Rate should be very high: 1 unit / 1 microsecond = 1,000,000 units/second
  REQUIRE(std::get<double>(sink.captured[0].value.value) == Catch::Approx(1000000.0));
}

TEST_CASE("RateMetric: summarize with no rates", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  // Summarize without any samples
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetRateSummaryData();
  REQUIRE(!summary.min_rate.has_value());
  REQUIRE(!summary.max_rate.has_value());
  REQUIRE(!summary.avg_rate.has_value());
  REQUIRE(sink.captured.empty());
}

TEST_CASE("RateMetric: rate calculation and processed samples", "[RateMetric]") {
  MockSampleSink   sink;
  astl::RateMetric metric = GetRateMetricWithSink(&sink);

  using nanoseconds = std::chrono::nanoseconds;

  // Add samples
  astl::AstlValue             val1{uint64_t{100}};
  astl::NormalizedSampledData sample1(1, val1, astl::ProcessedSampleTimestamp{nanoseconds{1'000'000'000}});
  metric.ReceiveRawSample(sample1);

  astl::AstlValue             val2{uint64_t{200}};
  astl::NormalizedSampledData sample2(2, val2, astl::ProcessedSampleTimestamp{nanoseconds{2'000'000'000}});
  metric.ReceiveRawSample(sample2);

  // Sample sink should have captured the rate values (not deltas)
  REQUIRE(sink.captured.size() == 1);
  REQUIRE(std::get<double>(sink.captured[0].value.value) == 100.0);
}
