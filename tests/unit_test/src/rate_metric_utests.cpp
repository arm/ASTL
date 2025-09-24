/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include <chrono>
#include <expected>
#include <limits>
#include <span>
#include <vector>

#include "../../test_includes.hpp"  // include before catch2
#include "metric/rate_metric.hpp"

// Test fixture class to access protected members
class RateMetricTestFixture : public astl::RateMetric {
 public:
  RateMetricTestFixture()
      : astl::RateMetric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr) {
  }

  // Expose protected methods for testing
  static std::expected<astl::AstlValue, astl_status_code> TestCalculateRate(const astl::AstlValue&    delta_value,
                                                                            std::chrono::microseconds time_interval) {
    return CalculateRate(delta_value, time_interval);
  }
};

TEST_CASE("RateMetric: construction", "[RateMetric]") {
  // Test basic construction
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  // Verify initial state
  auto summary = metric.GetRateSummaryData();
  REQUIRE(!summary.min_rate.has_value());
  REQUIRE(!summary.max_rate.has_value());

  auto rates = metric.GetRates();
  REQUIRE(rates.empty());
}

TEST_CASE("RateMetric: single sample - no rate calculated", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  // First sample should not produce a rate
  astl::AstlValue      val1{uint64_t{100}};
  astl::RawSampledData sample1(1, val1);
  auto                 status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Verify no rate was calculated
  auto summary = metric.GetRateSummaryData();
  REQUIRE(!summary.min_rate.has_value());
  REQUIRE(!summary.max_rate.has_value());

  auto rates = metric.GetRates();
  REQUIRE(rates.empty());
}

TEST_CASE("RateMetric: two samples - rate calculated", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  using microseconds = std::chrono::microseconds;

  // First sample
  astl::AstlValue      val1{uint64_t{100}};
  astl::RawSampledData sample1(1, val1, astl::SampleTimestamp{microseconds{1000000}});  // t=1s
  auto                 status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Second sample - should produce rate (delta=50, time=1s = 50 units/second)
  astl::AstlValue      val2{uint64_t{150}};
  astl::RawSampledData sample2(2, val2, astl::SampleTimestamp{microseconds{2000000}});  // t=2s
  auto                 status2 = metric.ReceiveRawSample(sample2);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);
  // Verify rate was calculated
  auto summary = metric.GetRateSummaryData();
  REQUIRE(summary.min_rate.has_value());
  REQUIRE(summary.max_rate.has_value());

  auto rates = metric.GetRates();
  REQUIRE(rates.size() == 1);

  // Rate should be 50 joules/second. Type is double since it's divided by time
  REQUIRE(rates[0].rate_value == Catch::Approx(50.0));
}

TEST_CASE("RateMetric: multiple samples - rate statistics", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  using microseconds = std::chrono::microseconds;

  // Sample 1: 100 joules at t=0
  astl::AstlValue      val1{uint64_t{100}};
  astl::RawSampledData sample1(1, val1, astl::SampleTimestamp{microseconds{0}});
  metric.ReceiveRawSample(sample1);

  // Sample 2: 200 joules at t=1s (rate = 100 J/s)
  astl::AstlValue      val2{uint64_t{200}};
  astl::RawSampledData sample2(2, val2, astl::SampleTimestamp{microseconds{1000000}});
  metric.ReceiveRawSample(sample2);

  // Sample 3: 250 joules at t=2s (rate = 50 J/s)
  astl::AstlValue      val3{uint64_t{250}};
  astl::RawSampledData sample3(3, val3, astl::SampleTimestamp{microseconds{2000000}});
  metric.ReceiveRawSample(sample3);

  // Sample 4: 400 joules at t=3s (rate = 150 J/s)
  astl::AstlValue      val4{uint64_t{400}};
  astl::RawSampledData sample4(4, val4, astl::SampleTimestamp{microseconds{3000000}});
  metric.ReceiveRawSample(sample4);

  // Verify statistics
  auto rates = metric.GetRates();
  REQUIRE(rates.size() == 3);

  // Verify individual rates. They should be in double after division by time
  REQUIRE(rates[0].rate_value == 100.0);  // First rate: 100.0 J/s
  REQUIRE(rates[1].rate_value == 50.0);   // Second rate: 50.0 J/s
  REQUIRE(rates[2].rate_value == 150.0);  // Third rate: 150.0 J/s

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
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  using microseconds = std::chrono::microseconds;

  // First sample
  astl::AstlValue      val1{uint64_t{100}};
  astl::RawSampledData sample1(1, val1, astl::SampleTimestamp{microseconds{1000000}});
  metric.ReceiveRawSample(sample1);

  // Second sample with same timestamp (zero time interval)
  astl::AstlValue      val2{uint64_t{150}};
  astl::RawSampledData sample2(2, val2, astl::SampleTimestamp{microseconds{1000000}});  // Same timestamp
  auto                 status = metric.ReceiveRawSample(sample2);

  // Should fail due to zero time interval
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);

  auto rates = metric.GetRates();
  REQUIRE(rates.empty());  // No rate should be calculated
}

TEST_CASE("RateMetric: invalid sample type handling", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  // Try to send a sample with wrong type (string instead of uint64)
  astl::AstlValue      invalid_val{std::string{"invalid"}};
  astl::RawSampledData invalid_sample(1, invalid_val);
  auto                 status = metric.ReceiveRawSample(invalid_sample);

  // Should fail due to type mismatch
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
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

TEST_CASE("RateMetric: CalculateRate with non-arithmetic value", "[RateMetric]") {
  // Test with non-arithmetic value (should fail)
  astl::AstlValue delta{std::string{"invalid"}};
  auto            time_interval = std::chrono::microseconds(1000000);

  auto result = RateMetricTestFixture::TestCalculateRate(delta, time_interval);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

TEST_CASE("RateMetric: GetRates span interface", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  using microseconds = std::chrono::microseconds;

  // Add multiple samples to generate rates
  for (int i = 0; i < 5; ++i) {
    astl::AstlValue      val{uint64_t{100 + (static_cast<uint64_t>(i) * 50)}};
    astl::RawSampledData sample(static_cast<uint16_t>(i + 1), val,
                                astl::SampleTimestamp{microseconds{static_cast<uint64_t>(i) * 1000000}});
    metric.ReceiveRawSample(sample);
  }

  auto rates = metric.GetRates();
  REQUIRE(rates.size() == 4);  // 5 samples should produce 4 rates

  // Verify we can iterate through the span
  int count = 0;
  for (const auto& rate_data : rates) {
    REQUIRE(rate_data.time_interval.count() > 0);
    count++;
  }
  REQUIRE(count == 4);
}

TEST_CASE("RateMetric: edge case - very small time intervals", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  using microseconds = std::chrono::microseconds;

  // First sample
  astl::AstlValue      val1{uint64_t{100}};
  astl::RawSampledData sample1(1, val1, astl::SampleTimestamp{microseconds{1000000}});
  metric.ReceiveRawSample(sample1);

  // Second sample with very small time difference (1 microsecond)
  astl::AstlValue      val2{uint64_t{101}};
  astl::RawSampledData sample2(2, val2, astl::SampleTimestamp{microseconds{1000001}});
  metric.ReceiveRawSample(sample2);

  auto rates = metric.GetRates();
  REQUIRE(rates.size() == 1);

  // Rate should be very high: 1 unit / 1 microsecond = 1,000,000 units/second
  REQUIRE(rates[0].rate_value == Catch::Approx(1000000.0));
}

TEST_CASE("RateMetric: summarize with no rates", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  // Summarize without any samples
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetRateSummaryData();
  REQUIRE(!summary.min_rate.has_value());
  REQUIRE(!summary.max_rate.has_value());
  REQUIRE(!summary.avg_rate.has_value());
}

TEST_CASE("RateMetric: inheritance from DeltaMetric", "[RateMetric]") {
  astl::RateMetric metric("test_rate", "unit-test rate metric", ASTL_UNITS_JOULES, ASTL_VALUE_UINT64, nullptr, nullptr);

  using microseconds = std::chrono::microseconds;

  // Add samples
  astl::AstlValue      val1{uint64_t{100}};
  astl::RawSampledData sample1(1, val1, astl::SampleTimestamp{microseconds{1000000}});
  metric.ReceiveRawSample(sample1);

  astl::AstlValue      val2{uint64_t{200}};
  astl::RawSampledData sample2(2, val2, astl::SampleTimestamp{microseconds{2000000}});
  metric.ReceiveRawSample(sample2);

  // Should have delta data from base class
  auto delta_summary = metric.GetDeltaSummaryData();
  REQUIRE(delta_summary.min_delta.has_value());
  REQUIRE(delta_summary.max_delta.has_value());

  auto deltas = metric.GetProcessedSamples();
  REQUIRE(deltas.size() == 1);
  REQUIRE(std::get<uint64_t>(deltas[0].value.value) == 100);

  // Should also have rate data
  auto rates = metric.GetRates();
  REQUIRE(rates.size() == 1);
  REQUIRE(rates[0].rate_value == 100.0);
}
