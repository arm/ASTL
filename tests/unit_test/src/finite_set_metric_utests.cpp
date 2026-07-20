// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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

  // create the target, mock  sink, metric config, and FiniteSetMetric instances based on the given state info
  explicit FiniteSetTestHarness(astl::FiniteSetMetricConfig::ValueToInfoMap const& state_info)
      : mock_target(std::make_unique<MockTarget>()),
        mock_sink(std::make_unique<MockProcessedSampleSink>()),
        config{
            "test_metric", "Test finite set metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_FINITE_SET_VALUE,
            ASTL_METRIC_IDENTIFIER_UNKNOWN, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{},
            // values of state_info as std::set<astl::AstlValue>
            astl::FiniteSetMetricConfig::FiniteSet{std::ranges::begin(state_info | std::views::keys),
                                                                                                     std::ranges::end(state_info | std::views::keys)},
            state_info
  },
        metric(&config, mock_target.get(), mock_sink.get()) {
    ALLOW_CALL(*mock_target, Name()).RETURN("mock_target");
    ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  }
};

TEST_CASE("FiniteSetMetric: construction & basic functionality", "[FiniteSetMetric]") {
  // Construct a metric for 64-bit unsigned samples
  // Provide labels for readability
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"STATE_ZERO", "Value is zero"}},
      {astl::AstlValue{uint64_t{1}}, {"STATE_ONE", "Value is one"}  },
      {astl::AstlValue{uint64_t{2}}, {"STATE_TWO", "Value is two"}  },
  };
  FiniteSetTestHarness harness{state_info};

  // Test that we can construct the metric successfully and call basic methods
  const auto& retrieved_set = harness.metric.GetFiniteSet();
  REQUIRE(retrieved_set.size() == 3);
}

TEST_CASE("FiniteSetMetric: finite set checking", "[FiniteSetMetric]") {
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"ZERO", "Zero state"}},
      {astl::AstlValue{uint64_t{1}}, {"ONE", "One state"}  },
      {astl::AstlValue{uint64_t{2}}, {"TWO", "Two state"}  },
  };
  FiniteSetTestHarness harness{state_info};

  // Test finite set membership
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{0}}) == true);
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{1}}) == true);
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{2}}) == true);

  // Test unknown value
  REQUIRE(harness.metric.IsInFiniteSet(astl::AstlValue{uint64_t{99}}) == false);
}

TEST_CASE("FiniteSetMetric: get finite set values", "[FiniteSetMetric]") {
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"VAL0", "Value 0"}},
      {astl::AstlValue{uint64_t{1}}, {"VAL1", "Value 1"}},
      {astl::AstlValue{uint64_t{2}}, {"VAL2", "Value 2"}},
  };
  FiniteSetTestHarness harness{state_info};

  const auto& retrieved_set = harness.metric.GetFiniteSet();
  REQUIRE(retrieved_set.size() == 3);
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{0}}));
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{1}}));
  REQUIRE(retrieved_set.contains(astl::AstlValue{uint64_t{2}}));
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: ReceiveSample with valid values", "[FiniteSetMetric]") {
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"S0", "Sample state 0"}},
      {astl::AstlValue{uint64_t{1}}, {"S1", "Sample state 1"}},
      {astl::AstlValue{uint64_t{2}}, {"S2", "Sample state 2"}},
  };
  FiniteSetTestHarness harness{state_info};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add samples from the finite set
  std::vector<uint64_t> sample_values = {0, 1, 1, 2, 0, 1, 2, 2, 2};

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::NormalizedSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    auto                        status = harness.metric.ReceiveRawSample(sample);
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
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"A", "State A"}},
      {astl::AstlValue{uint64_t{1}}, {"B", "State B"}},
      {astl::AstlValue{uint64_t{2}}, {"C", "State C"}},
  };
  FiniteSetTestHarness harness{state_info};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add samples including unknown values
  std::vector<uint64_t> sample_values = {0, 1, 5, 2, 10, 1, 0};  // 5 and 10 are unknown

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::NormalizedSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
    auto                        status = harness.metric.ReceiveRawSample(sample);
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
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"RESET0", "Reset state 0"}},
      {astl::AstlValue{uint64_t{1}}, {"RESET1", "Reset state 1"}},
      {astl::AstlValue{uint64_t{2}}, {"RESET2", "Reset state 2"}},
  };
  FiniteSetTestHarness harness{state_info};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add some samples
  astl::NormalizedSampledData sample1(1, astl::AstlValue{uint64_t{0}});
  astl::NormalizedSampledData sample2(2, astl::AstlValue{uint64_t{1}});

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
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"SUM0", "Summary state 0"}},
      {astl::AstlValue{uint64_t{1}}, {"SUM1", "Summary state 1"}},
      {astl::AstlValue{uint64_t{2}}, {"SUM2", "Summary state 2"}},
  };
  FiniteSetTestHarness harness{state_info};
  ALLOW_CALL(*(harness.mock_sink), SinkProcessedSamples(trompeloeil::_, trompeloeil::_, trompeloeil::_))
      .RETURN(ASTL_STATUS_SUCCESS);

  // Add some samples
  std::vector<uint64_t> sample_values = {0, 1, 2, 1, 0};

  for (size_t i = 0; i < sample_values.size(); ++i) {
    astl::NormalizedSampledData sample(static_cast<uint16_t>(i), astl::AstlValue{sample_values[i]});
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
  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"U64_0", "Unsigned 64-bit state 0"}},
      {astl::AstlValue{uint64_t{1}}, {"U64_1", "Unsigned 64-bit state 1"}},
      {astl::AstlValue{uint64_t{2}}, {"U64_2", "Unsigned 64-bit state 2"}},
  };
  FiniteSetTestHarness harness{state_info};

  // Try to send a FLOAT32 value (which should be rejected by the base class, since harness is build with UINT64)
  astl::AstlValue             val{float{40.0}};
  astl::NormalizedSampledData sample(1, val);
  auto                        status = harness.metric.ReceiveRawSample(sample);
  REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("FiniteSetMetric: Boolean values handling", "[FiniteSetMetric]") {
  // Define a finite set with boolean values
  astl::FiniteSetMetric::FiniteSet finite_set = {astl::AstlValue{false}, astl::AstlValue{true}};

  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{false}, {"OFF", "Power is off"}},
      {astl::AstlValue{true},  {"ON", "Power is on"}  },
  };
  astl::FiniteSetMetricConfig config{"power_level",
                                     "Power level states",
                                     ASTL_UNITS_WATTS,
                                     ASTL_VALUE_BOOL8,
                                     ASTL_METRIC_FINITE_SET_VALUE,
                                     ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                     astl::CollectorType::UNKNOWN,
                                     astl::NullOperationBuilder{},
                                     finite_set,
                                     state_info};

  astl::FiniteSetMetric metric(&config, nullptr, nullptr);

  astl::NormalizedSampledData sample1(1, astl::AstlValue{false});
  astl::NormalizedSampledData sample2(2, astl::AstlValue{true});
  astl::NormalizedSampledData sample3(3, astl::AstlValue{true});
  astl::NormalizedSampledData sample4(4, astl::AstlValue{uint64_t{2}});  // Not in finite set

  REQUIRE(metric.ReceiveRawSample(sample1) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(sample2) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(sample3) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(sample4) == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);

  auto summary = metric.GetFiniteSetSummaryData();
  REQUIRE(summary.total_samples == 3);
  REQUIRE(summary.unknown_values == 0);

  // Check finite set membership
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{false}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{true}) == true);
  REQUIRE(metric.IsInFiniteSet(astl::AstlValue{uint64_t{2}}) == false);

  // Check value counts
  REQUIRE(summary.value_counts.at(astl::AstlValue{false}) == 1);
  REQUIRE(summary.value_counts.at(astl::AstlValue{true}) == 2);
  REQUIRE(!summary.value_counts.contains(astl::AstlValue{uint64_t{2}}));
}
// NOLINTEND(readability-function-cognitive-complexity)
