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

/**
 * @file finite_set_metric_utests.cpp
 * @brief Unit tests for FiniteSetMetric class
 */

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../test_includes.hpp"  // include before catch2
#include "metric/finite_set_metric.hpp"

TEST_CASE("FiniteSetMetric: construction & basic functionality", "[FiniteSetMetric]") {
  // Define a simple finite set for testing
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  // Construct a metric for 64-bit unsigned samples
  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  // Test that we can construct the metric successfully and call basic methods
  const auto& retrieved_set = metric.GetFiniteSet();
  REQUIRE(retrieved_set.size() == 3);
}

TEST_CASE("FiniteSetMetric: finite set checking", "[FiniteSetMetric]") {
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  // Test finite set membership
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{0}}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{1}}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{2}}) == true);

  // Test unknown value
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{99}}) == false);
}

TEST_CASE("FiniteSetMetric: get finite set values", "[FiniteSetMetric]") {
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  const auto& retrieved_set = metric.GetFiniteSet();
  REQUIRE(retrieved_set.size() == 3);
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{0}}));
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{1}}));
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{2}}));
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: ReceiveSample with valid values", "[FiniteSetMetric]") {
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  // Add samples from the finite set
  std::vector<uint64_t> sample_values = {0, 1, 1, 2, 0, 1, 2, 2, 2};

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::RawSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    auto                 status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Check summary data
  auto summary = metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 9);
  REQUIRE(summary.unknown_values == 0);

  // Check value counts: {0: 2 times, 1: 3 times, 2: 4 times}
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{0}}) == 2);
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{1}}) == 3);
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{2}}) == 4);
}
// NOLINTEND(readability-function-cognitive-complexity)

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: ReceiveSample with unknown values", "[FiniteSetMetric]") {
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  // Add samples including unknown values
  std::vector<uint64_t> sample_values = {0, 1, 5, 2, 10, 1, 0};  // 5 and 10 are unknown

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::RawSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    auto                 status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  auto summary = metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 7);
  REQUIRE(summary.unknown_values == 2);  // Values 5 and 10 are unknown

  // Check that unknown values are still counted in value_counts
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{5}}) == 1);
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{10}}) == 1);

  // Verify that unknown values are not in the finite set
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{5}}) == false);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{10}}) == false);
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("FiniteSetMetric: Reset functionality", "[FiniteSetMetric]") {
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  // Add some samples
  astl::RawSampledData sample1(1, astl::AstlValue{uint64_t{0}});
  astl::RawSampledData sample2(2, astl::AstlValue{uint64_t{1}});

  metric.ReceiveRawSample(sample1);
  metric.ReceiveRawSample(sample2);

  auto summary_before = metric.GetFiniteSetSummaryData();
  REQUIRE(summary_before.total_samples == 2);

  // Reset the metric
  metric.Reset();

  auto summary_after = metric.GetFiniteSetSummaryData();
  REQUIRE(summary_after.total_samples == 0);
  REQUIRE(summary_after.unknown_values == 0);
  REQUIRE(summary_after.value_counts.empty());
}

TEST_CASE("FiniteSetMetric: Summarize operation", "[FiniteSetMetric]") {
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint64_t{0}}, astl::AstlValue{uint64_t{1}},
                                          astl::AstlValue{uint64_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, finite_set,
                               nullptr, nullptr);

  // Add some samples
  std::vector<uint64_t> sample_values = {0, 1, 2, 1, 0};

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::RawSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    metric.ReceiveRawSample(sample);
  }

  // Summarize should succeed and not change the data
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 5);
}

TEST_CASE("FiniteSetMetric: ReceiveSample with unsupported type", "[FiniteSetMetric]") {
  // Create a metric expecting UINT32 values
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{uint32_t{0}}, astl::AstlValue{uint32_t{1}},
                                          astl::AstlValue{uint32_t{2}}};

  astl::FiniteSetMetric metric("test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT32, finite_set,
                               nullptr, nullptr);

  // Try to send a UINT64 value (which should be rejected by the base class)
  astl::AstlValue      val{uint64_t{40}};
  astl::RawSampledData sample(1, val);
  auto                 status = metric.ReceiveRawSample(sample);
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: String values handling", "[FiniteSetMetric]") {
  // Define a finite set with string values
  std::set<astl::AstlValue> finite_set = {astl::AstlValue{std::string{"LOW"}}, astl::AstlValue{std::string{"MEDIUM"}},
                                          astl::AstlValue{std::string{"HIGH"}}};

  astl::FiniteSetMetric metric("power_level", "Power level states", ASTL_UNITS_WATTS, ASTL_VALUE_STRING, finite_set,
                               nullptr, nullptr);

  astl::RawSampledData sample1(1, astl::AstlValue{std::string{"LOW"}});
  astl::RawSampledData sample2(2, astl::AstlValue{std::string{"MEDIUM"}});
  astl::RawSampledData sample3(3, astl::AstlValue{std::string{"HIGH"}});
  astl::RawSampledData sample4(4, astl::AstlValue{std::string{"UNKNOWN"}});  // Not in finite set

  REQUIRE(metric.ReceiveRawSample(sample1) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(sample2) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(sample3) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(sample4) == ASTL_STATUS_SUCCESS);

  auto summary = metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 4);
  REQUIRE(summary.unknown_values == 1);  // "UNKNOWN" is not in the finite set

  // Check finite set membership
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{std::string{"LOW"}}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{std::string{"MEDIUM"}}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{std::string{"HIGH"}}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{std::string{"UNKNOWN"}}) == false);

  // Check value counts
  REQUIRE(summary.value_counts.at(astl::AstlValue{std::string{"LOW"}}) == 1);
  REQUIRE(summary.value_counts.at(astl::AstlValue{std::string{"MEDIUM"}}) == 1);
  REQUIRE(summary.value_counts.at(astl::AstlValue{std::string{"HIGH"}}) == 1);
  REQUIRE(summary.value_counts.at(astl::AstlValue{std::string{"UNKNOWN"}}) == 1);
}
// NOLINTEND(readability-function-cognitive-complexity)
