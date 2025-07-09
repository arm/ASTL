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

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <limits>
#include <span>
#include <vector>

#include "metric/sampled_delta_metric.hpp"

// Test fixture class to access protected members
class DeltaMetricTestFixture : public astl::DeltaMetric {
 public:
  DeltaMetricTestFixture()
      : astl::DeltaMetric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64) {}

  // Expose protected methods for testing
  static std::expected<astl::AstlValue, astl_status_code> TestCalculateDelta(const astl::AstlValue& current_sample,
                                                                             const astl::AstlValue& previous_sample) {
    return CalculateDelta(current_sample, previous_sample);
  }

  astl_status_code TestUpdateDeltaStatistics(const astl::AstlValue& delta_value, astl::SampleTimestamp timestamp) {
    return UpdateDeltaStatistics(delta_value, timestamp);
  }
};

TEST_CASE("DeltaMetric: construction", "[DeltaMetric]") {
  // Test basic construction
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // Verify initial state
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  auto deltas = metric.GetDeltas();
  REQUIRE(deltas.empty());
}

TEST_CASE("DeltaMetric: single sample - no delta calculated", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // First sample should not produce a delta
  astl::AstlValue   val1{uint64_t{100}};
  astl::SampledData sample1(1, val1);
  auto              status1 = metric.ReceiveSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Verify no delta was calculated
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  auto deltas = metric.GetDeltas();
  REQUIRE(deltas.empty());
}

TEST_CASE("DeltaMetric: two samples - delta calculated", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // First sample
  astl::AstlValue   val1{uint64_t{100}};
  astl::SampledData sample1(1, val1);
  auto              status1 = metric.ReceiveSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);

  // Second sample - should produce delta
  astl::AstlValue   val2{uint64_t{150}};
  astl::SampledData sample2(2, val2);
  auto              status2 = metric.ReceiveSample(sample2);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);

  // Verify delta was calculated (150 - 100 = 50)
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(summary.min_delta.has_value());
  REQUIRE(summary.max_delta.has_value());

  auto deltas = metric.GetDeltas();
  REQUIRE(deltas.size() == 1);

  auto delta_value = std::get<uint64_t>(deltas[0].delta_value.value);
  REQUIRE(delta_value == 50);
}

TEST_CASE("DeltaMetric: multiple samples - deltas calculated", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // Feed multiple samples with known deltas
  std::vector<uint64_t> values = {100, 150, 170, 180, 200};
  // Expected deltas: 50, 20, 10, 20
  std::vector<uint64_t> expected_deltas = {50, 20, 10, 20};

  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue   sample_value{uint64_t{values[i]}};
    astl::SampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto              status = metric.ReceiveSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Verify deltas were calculated correctly
  auto deltas = metric.GetDeltas();
  REQUIRE(deltas.size() == 4);

  // Check each delta value
  for (size_t i = 0; i < expected_deltas.size(); ++i) {
    auto delta_value = std::get<uint64_t>(deltas[i].delta_value.value);
    REQUIRE(delta_value == expected_deltas[i]);
  }
}

TEST_CASE("DeltaMetric: invalid sample type", "[DeltaMetric]") {
  // Create metric expecting UINT32 samples
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT32);

  // Try to feed a UINT64 sample
  astl::AstlValue   val1{uint64_t{100}};
  astl::SampledData sample1(1, val1);
  auto              status1 = metric.ReceiveSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);

  // Verify no delta was calculated
  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  auto deltas = metric.GetDeltas();
  REQUIRE(deltas.empty());
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

  SECTION("Non-arithmetic values") {
    astl::AstlValue current{std::string{"test"}};
    astl::AstlValue previous{std::string{"prev"}};

    auto result = DeltaMetricTestFixture::TestCalculateDelta(current, previous);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }

  SECTION("Mixed arithmetic and non-arithmetic") {
    astl::AstlValue current{uint64_t{150}};
    astl::AstlValue previous{std::string{"prev"}};

    auto result = DeltaMetricTestFixture::TestCalculateDelta(current, previous);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }
}

TEST_CASE("DeltaMetric: different value types", "[DeltaMetric]") {
  SECTION("UINT32 values") {
    astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT32);

    astl::AstlValue   val1{uint32_t{100}};
    astl::AstlValue   val2{uint32_t{150}};
    astl::SampledData sample1(1, val1);
    astl::SampledData sample2(2, val2);

    auto status1 = metric.ReceiveSample(sample1);
    auto status2 = metric.ReceiveSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    auto deltas = metric.GetDeltas();
    REQUIRE(deltas.size() == 1);

    auto delta_value = std::get<uint32_t>(deltas[0].delta_value.value);
    REQUIRE(delta_value == 50);
  }

  SECTION("Large UINT64 values") {
    astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

    astl::AstlValue   val1{uint64_t{100}};
    astl::AstlValue   val2{uint64_t{150}};
    astl::SampledData sample1(1, val1);
    astl::SampledData sample2(2, val2);

    auto status1 = metric.ReceiveSample(sample1);
    auto status2 = metric.ReceiveSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    auto deltas = metric.GetDeltas();
    REQUIRE(deltas.size() == 1);

    auto delta_value = std::get<uint64_t>(deltas[0].delta_value.value);
    REQUIRE(delta_value == 50);
  }

  SECTION("FLOAT64 values") {
    astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_FLOAT64);

    astl::AstlValue   val1{100.5};
    astl::AstlValue   val2{150.5};
    astl::SampledData sample1(1, val1);
    astl::SampledData sample2(2, val2);

    auto status1 = metric.ReceiveSample(sample1);
    auto status2 = metric.ReceiveSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    auto deltas = metric.GetDeltas();
    REQUIRE(deltas.size() == 1);

    auto delta_value = std::get<double>(deltas[0].delta_value.value);
    REQUIRE(delta_value == 50.0);
  }
}

TEST_CASE("DeltaMetric: edge cases", "[DeltaMetric]") {
  SECTION("Zero delta") {
    astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

    astl::AstlValue   val1{uint64_t{100}};
    astl::AstlValue   val2{uint64_t{100}};
    astl::SampledData sample1(1, val1);
    astl::SampledData sample2(2, val2);

    auto status1 = metric.ReceiveSample(sample1);
    auto status2 = metric.ReceiveSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    auto deltas = metric.GetDeltas();
    REQUIRE(deltas.size() == 1);

    auto delta_value = std::get<uint64_t>(deltas[0].delta_value.value);
    REQUIRE(delta_value == 0);
  }

  SECTION("Large delta values") {
    astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

    astl::AstlValue   val1{uint64_t{0}};
    astl::AstlValue   val2{std::numeric_limits<uint64_t>::max()};
    astl::SampledData sample1(1, val1);
    astl::SampledData sample2(2, val2);

    auto status1 = metric.ReceiveSample(sample1);
    auto status2 = metric.ReceiveSample(sample2);

    REQUIRE(status1 == ASTL_STATUS_SUCCESS);
    REQUIRE(status2 == ASTL_STATUS_SUCCESS);

    auto deltas = metric.GetDeltas();
    REQUIRE(deltas.size() == 1);

    auto delta_value = std::get<uint64_t>(deltas[0].delta_value.value);
    REQUIRE(delta_value == std::numeric_limits<uint64_t>::max());
  }
}

TEST_CASE("DeltaMetric: summarize calculates statistics", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // Feed samples with known deltas
  std::vector<uint64_t> values = {100, 110, 120, 130};
  // Expected deltas: 10, 10, 10 (all positive, same value)

  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue   sample_value{uint64_t{values[i]}};
    astl::SampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto              status = metric.ReceiveSample(sample);
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
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_FLOAT64);

  // Feed samples with varying deltas
  std::vector<double> values = {100.0, 110.0, 105.0, 125.0, 120.0};
  // Expected deltas: 10, -5, 20, -5

  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue   sample_value{double{values[i]}};
    astl::SampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto              status = metric.ReceiveSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Summarize to calculate statistics
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetDeltaSummaryData();
  auto deltas  = metric.GetDeltas();
  REQUIRE(deltas.size() == 4);

  // With signed arithmetic, we expect min to be -5 and max to be 20
  auto min_delta = std::get<double>(summary.min_delta.value().value);
  auto max_delta = std::get<double>(summary.max_delta.value().value);

  REQUIRE(min_delta == -5.0);
  REQUIRE(max_delta == 20.0);
}

TEST_CASE("DeltaMetric: no samples summarize", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // Summarize without any samples
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetDeltaSummaryData();
  REQUIRE(!summary.min_delta.has_value());
  REQUIRE(!summary.max_delta.has_value());

  auto deltas = metric.GetDeltas();
  REQUIRE(deltas.empty());
}

TEST_CASE("DeltaMetric: Reset functionality", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // Feed some samples to create state
  std::vector<uint64_t> values = {100, 150, 200};
  for (size_t i = 0; i < values.size(); ++i) {
    astl::AstlValue   sample_value{uint64_t{values[i]}};
    astl::SampledData sample(static_cast<astl::OperationId>(i + 1), sample_value);
    auto              status = metric.ReceiveSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Verify state exists
  auto deltas_before = metric.GetDeltas();
  REQUIRE(deltas_before.size() == 2);

  // Reset the metric
  metric.Reset();

  // Verify state is cleared
  auto deltas_after = metric.GetDeltas();
  REQUIRE(deltas_after.empty());
}

TEST_CASE("DeltaMetric: GetSamples returns empty span", "[DeltaMetric]") {
  astl::DeltaMetric metric("test_delta", "unit-test delta metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);

  // Test GetSamples when no samples have been received
  auto samples_empty = metric.GetSamples();
  REQUIRE(samples_empty.empty());

  // Add some samples
  astl::AstlValue   val1{uint64_t{100}};
  astl::AstlValue   val2{uint64_t{150}};
  astl::SampledData sample1(1, val1);
  astl::SampledData sample2(2, val2);

  auto status1 = metric.ReceiveSample(sample1);
  auto status2 = metric.ReceiveSample(sample2);

  REQUIRE(status1 == ASTL_STATUS_SUCCESS);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);

  // DeltaMetric doesn't store original samples, so GetSamples should still return empty
  auto samples_after = metric.GetSamples();
  REQUIRE(samples_after.empty());
}