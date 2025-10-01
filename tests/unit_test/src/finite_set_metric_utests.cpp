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

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "metric/finite_set_metric.hpp"

// Everything needed for most of the unit test in this module. Nice mocks for target and sink, without strict
// expectations
struct FiniteSetTestHarness {
  std::unique_ptr<MockTarget>              mock_target;
  std::unique_ptr<MockProcessedSampleSink> mock_sink;
  astl::FiniteSetMetricConfig              config;
  astl::FiniteSetMetric                    metric;

  // create the target, mock  sink, metric config, and FiniteSetMetric instances based on the given labels
  explicit FiniteSetTestHarness(astl::FiniteSetMetric::ValueToLabel const& labels)
      : mock_target(std::make_unique<MockTarget>()),
        mock_sink(std::make_unique<MockProcessedSampleSink>()),
        config{
            "test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_FINITE_SET_VALUE,
            astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{},
            // values of labels as std::set<astl::AstlValue>
            astl::FiniteSetMetricConfig::FiniteSet{std::ranges::begin(labels | std::views::keys),
                                                                     std::ranges::end(labels | std::views::keys)},
            labels
  },
        metric(&config, mock_target.get(), mock_sink.get()) {
    ALLOW_CALL(*mock_target, Name()).RETURN("mock_target");
    ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  }
};

TEST_CASE("FiniteSetMetric: construction & basic functionality", "[FiniteSetMetric]") {
  // Construct a metric for 64-bit unsigned samples
  // Provide labels for readability
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "STATE_ZERO"},
      {astl::AstlValue{uint64_t{1}}, "STATE_ONE" },
      {astl::AstlValue{uint64_t{2}}, "STATE_TWO" },
  };
  FiniteSetTestHarness harness{labels};

  // Test that we can construct the metric successfully and call basic methods
  const auto& retrieved_set = harness.metric.GetFiniteSet();
  REQUIRE(retrieved_set.size() == 3);
}

TEST_CASE("FiniteSetMetric: finite set checking", "[FiniteSetMetric]") {
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "ZERO"},
      {astl::AstlValue{uint64_t{1}}, "ONE" },
      {astl::AstlValue{uint64_t{2}}, "TWO" },
  };
  FiniteSetTestHarness harness{labels};

  // Test finite set membership
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{0}}) == true);
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{1}}) == true);
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{2}}) == true);

  // Test unknown value
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{99}}) == false);
}

TEST_CASE("FiniteSetMetric: get finite set values", "[FiniteSetMetric]") {
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "VAL0"},
      {astl::AstlValue{uint64_t{1}}, "VAL1"},
      {astl::AstlValue{uint64_t{2}}, "VAL2"},
  };
  FiniteSetTestHarness harness{labels};

  const auto& retrieved_set = harness.metric.GetFiniteSet();
  REQUIRE(retrieved_set.size() == 3);
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{0}}));
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{1}}));
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{2}}));
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: ReceiveSample with valid values", "[FiniteSetMetric]") {
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "S0"},
      {astl::AstlValue{uint64_t{1}}, "S1"},
      {astl::AstlValue{uint64_t{2}}, "S2"},
  };
  FiniteSetTestHarness harness{labels};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add samples from the finite set
  std::vector<uint64_t> sample_values = {0, 1, 1, 2, 0, 1, 2, 2, 2};

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::RawSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    auto                 status = harness.metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Check summary data
  auto summary = harness.metric.GetFiniteSetSummaryData();
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
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "A"},
      {astl::AstlValue{uint64_t{1}}, "B"},
      {astl::AstlValue{uint64_t{2}}, "C"},
  };
  FiniteSetTestHarness harness{labels};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add samples including unknown values
  std::vector<uint64_t> sample_values = {0, 1, 5, 2, 10, 1, 0};  // 5 and 10 are unknown

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::RawSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    auto                 status = harness.metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  auto summary = harness.metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 7);
  REQUIRE(summary.unknown_values == 2);  // Values 5 and 10 are unknown

  // Check that unknown values are still counted in value_counts
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{5}}) == 1);
  REQUIRE(summary.value_counts.at(astl::AstlValue{uint64_t{10}}) == 1);

  // Verify that unknown values are not in the finite set
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{5}}) == false);
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{10}}) == false);
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("FiniteSetMetric: Reset functionality", "[FiniteSetMetric]") {
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "RESET0"},
      {astl::AstlValue{uint64_t{1}}, "RESET1"},
      {astl::AstlValue{uint64_t{2}}, "RESET2"},
  };
  FiniteSetTestHarness harness{labels};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add some samples
  astl::RawSampledData sample1(1, astl::AstlValue{uint64_t{0}});
  astl::RawSampledData sample2(2, astl::AstlValue{uint64_t{1}});

  harness.metric.ReceiveRawSample(sample1);
  harness.metric.ReceiveRawSample(sample2);

  auto summary_before = harness.metric.GetFiniteSetSummaryData();
  REQUIRE(summary_before.total_samples == 2);

  // Reset the metric
  harness.metric.Reset();

  auto summary_after = harness.metric.GetFiniteSetSummaryData();
  REQUIRE(summary_after.total_samples == 0);
  REQUIRE(summary_after.unknown_values == 0);
  REQUIRE(summary_after.value_counts.empty());
}

TEST_CASE("FiniteSetMetric: Summarize operation", "[FiniteSetMetric]") {
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "SUM0"},
      {astl::AstlValue{uint64_t{1}}, "SUM1"},
      {astl::AstlValue{uint64_t{2}}, "SUM2"},
  };
  FiniteSetTestHarness harness{labels};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add some samples
  std::vector<uint64_t> sample_values = {0, 1, 2, 1, 0};

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::RawSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    harness.metric.ReceiveRawSample(sample);
  }

  // Summarize should succeed and not change the data
  auto status = harness.metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = harness.metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 5);
}

TEST_CASE("FiniteSetMetric: ReceiveSample with unsupported type", "[FiniteSetMetric]") {
  // Create a metric expecting UINT64 values
  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "U64_0"},
      {astl::AstlValue{uint64_t{1}}, "U64_1"},
      {astl::AstlValue{uint64_t{2}}, "U64_2"},
  };
  FiniteSetTestHarness harness{labels};

  // Try to send a FLOAT32 value (which should be rejected by the base class, since harness is build with UINT64)
  astl::AstlValue      val{float{40.0}};
  astl::RawSampledData sample(1, val);
  auto                 status = harness.metric.ReceiveRawSample(sample);
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: String values handling", "[FiniteSetMetric]") {
  // Define a finite set with string values
  astl::FiniteSetMetric::FiniteSet finite_set = {astl::AstlValue{std::string{"LOW"}},
                                                 astl::AstlValue{std::string{"MEDIUM"}},
                                                 astl::AstlValue{std::string{"HIGH"}}};

  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{std::string{"LOW"}},    "LOW_L" },
      {astl::AstlValue{std::string{"MEDIUM"}}, "MED_L" },
      {astl::AstlValue{std::string{"HIGH"}},   "HIGH_L"},
  };
  astl::FiniteSetMetricConfig config{"power_level",
                                     "Power level states",
                                     ASTL_UNITS_WATTS,
                                     ASTL_VALUE_STRING,
                                     ASTL_METRIC_FINITE_SET_VALUE,
                                     astl::CollectorType::UNKNOWN,
                                     astl::NullOperationBuilder{},
                                     finite_set,
                                     labels};

  astl::FiniteSetMetric metric(&config, nullptr, nullptr);

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
