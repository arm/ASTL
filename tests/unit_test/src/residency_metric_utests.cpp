/******************************************************************************
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

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <span>
#include <vector>

#include "metric/residency_metric.hpp"

using namespace std::chrono_literals;

static std::vector<astl::ResidencyMetricConfig::StateInfo> CreateTestStateInfos(double frequency = 1000000.0) {
  return {
      {"C6", frequency, astl::ScmiOperationBuilder{0x67DE}}, // C6 state with specified frequency
      {"C1", frequency, astl::ScmiOperationBuilder{0x68DE}}, // C1 state with specified frequency
      {"C2", frequency, astl::ScmiOperationBuilder{0x69DE}}, // C2 state with specified frequency
  };
}

static auto GetStatetoInfoMap() -> const astl::ResidencyMetricConfig::StateToInfoMap& {
  static astl::ResidencyMetricConfig::StateToInfoMap state_info = {
      {"C6", {"C6", 1000000.0, astl::ScmiOperationBuilder{0x67DE}}},
      {"C1", {"C1", 1000000.0, astl::ScmiOperationBuilder{0x68DE}}},
      {"C2", {"C2", 1000000.0, astl::ScmiOperationBuilder{0x69DE}}}
  };
  return state_info;
}

static const astl::ResidencyMetricConfig* GetResidencyConfig() {
  static astl::ResidencyMetricConfig config{"test_cpu_cstate_residency",
                                            "Unit test CPU C-state residency metric",
                                            ASTL_UNITS_TICKS,
                                            ASTL_VALUE_UINT64,
                                            ASTL_METRIC_RESIDENCY,
                                            ASTL_CATEGORY_UNCATEGORIZED,
                                            astl::CollectorType::SCMI,
                                            GetStatetoInfoMap(),
                                            "Active"};
  return &config;
}

static astl::ResidencyMetric GetResidencyMetric() {
  return astl::ResidencyMetric{GetResidencyConfig(), CreateTestStateInfos(), nullptr, nullptr};
}

// Test fixture class to access protected members
class ResidencyMetricTestFixture : public astl::ResidencyMetric {
 public:
  ResidencyMetricTestFixture()
      : astl::ResidencyMetric(GetResidencyConfig(), CreateTestStateInfos(), nullptr, nullptr) {}

  // Constructor with custom frequency
  explicit ResidencyMetricTestFixture(double frequency)
      : astl::ResidencyMetric(GetResidencyConfig(), CreateTestStateInfos(frequency), nullptr, nullptr) {}

  // Expose protected methods for testing
  static std::expected<std::chrono::microseconds, astl_status_code> TestConvertTicksToMicroseconds(
      const astl::AstlValue& ticks, const astl::ResidencyMetricConfig::StateInfo& config) {
    return astl::ResidencyMetric::ConvertTicksToMicroseconds(ticks, config);
  }

  static std::expected<double, astl_status_code> TestCalculatePercentage(std::chrono::microseconds time_in_state,
                                                                         std::chrono::microseconds total_interval) {
    return astl::ResidencyMetric::CalculatePercentage(time_in_state, total_interval);
  }

  astl_status_code TestUpdateStateResidencyStatistics(const std::string&        state_name,
                                                      std::chrono::microseconds time_microseconds, double percentage,
                                                      astl::SampleTimestamp timestamp) {
    return UpdateStateResidencyStatistics(state_name, time_microseconds, percentage, timestamp);
  }

  // Helper method to create properly typed timestamps
  static astl::SampleTimestamp CreateTimestamp(std::chrono::steady_clock::time_point timePoint) {
    return std::chrono::time_point_cast<astl::SampleTimestamp::duration>(timePoint);
  }
};

// Recording sink capturing microsecond values in the order delivered. Does not assume
// how many samples arrive per sink invocation (future batching friendly).
struct RecordingProcessedSampleSink : public astl::IProcessedSampleSink {
  std::vector<uint64_t> values_in_order;  // microsecond values in sink order
  astl_status_code      SinkProcessedSamples(const astl::ITarget* target, const astl::IMetric* metric,
                                             std::span<const astl::ProcessedSampledData> samples) override {
    (void)target;  // unused
    (void)metric;  // unused
    for (const auto& sample : samples) {
      REQUIRE(std::holds_alternative<uint64_t>(sample.value.value));
      values_in_order.push_back(std::get<uint64_t>(sample.value.value));
    }
    return ASTL_STATUS_SUCCESS;
  }
};

TEST_CASE("ResidencyMetric: deterministic processed sample sink order", "[ResidencyMetric]") {
  // Setup recording sink
  RecordingProcessedSampleSink recording_sink;

  // Build custom metric with sink registered
  astl::ResidencyMetric metric{GetResidencyConfig(), CreateTestStateInfos(), nullptr, &recording_sink};

  // Acquire operations and baseline samples for all states to initialize previous samples
  auto operations_result = metric.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(operations.size() == 3);  // C6, C1, C2

  // Baseline timestamp (all share same baseline so no interval yet)
  auto base_time  = std::chrono::steady_clock::now();
  auto timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(base_time);

  // Initial baseline samples (no processed samples expected yet)
  metric.ReceiveRawSample(astl::RawSampledData(operations[0]->GetId(), astl::AstlValue{uint64_t{1000}}, timestamp1));
  metric.ReceiveRawSample(astl::RawSampledData(operations[1]->GetId(), astl::AstlValue{uint64_t{500}}, timestamp1));
  metric.ReceiveRawSample(astl::RawSampledData(operations[2]->GetId(), astl::AstlValue{uint64_t{250}}, timestamp1));
  REQUIRE(recording_sink.values_in_order.empty());

  // Second timestamp (delta interval)
  auto timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(50));

  // Provide second samples in an intentionally shuffled order (not matching configured order) to verify
  // that sink order is independent from arrival order.
  // Arrival order: C1, C2, C6
  metric.ReceiveRawSample(
      astl::RawSampledData(operations[1]->GetId(), astl::AstlValue{uint64_t{525}}, timestamp2));  // C1 delta 25
  metric.ReceiveRawSample(
      astl::RawSampledData(operations[2]->GetId(), astl::AstlValue{uint64_t{265}}, timestamp2));  // C2 delta 15
  // At this point not all states processed yet -> no sinking
  REQUIRE(recording_sink.values_in_order.empty());
  metric.ReceiveRawSample(astl::RawSampledData(operations[0]->GetId(), astl::AstlValue{uint64_t{1050}},
                                               timestamp2));  // C6 delta 50 (triggers flush)

  // After processing all three, inferred state (Active) also added => total samples sunk should be 4.
  REQUIRE(recording_sink.values_in_order.size() == 4);

  // Retrieve configured deterministic order
  auto configured_order = metric.GetOrderedStates();
  // configured_order should have 4 entries: C6, C1, C2, Active
  REQUIRE(configured_order.size() == 4);
  REQUIRE(configured_order[0] == "C6");
  REQUIRE(configured_order[1] == "C1");
  REQUIRE(configured_order[2] == "C2");
  REQUIRE(configured_order[3] == "Active");

  // Pre-computed expected microsecond residency per state in deterministic order (C6, C1, C2, Active):
  // Frequencies = 1,000,000 Hz so ticks == microseconds.
  // Deltas: C6=50, C1=25, C2=15; Interval length = 50ms = 50,000us -> Active = 50,000 - (50+25+15) = 49,910
  const std::vector<uint64_t> expected_values_in_order{50, 25, 15, 49910};
  REQUIRE(recording_sink.values_in_order == expected_values_in_order);
}

TEST_CASE("ResidencyMetric: construction", "[ResidencyMetric]") {
  auto metric = GetResidencyMetric();

  // Verify initial state
  auto summary = metric.GetResidencySummaryData();
  REQUIRE(summary.total_time_seconds.empty());
  REQUIRE(summary.average_percentage.empty());
  REQUIRE(!summary.inferred_state_percentage.has_value());

  auto residency_data = metric.GetResidencyData();
  REQUIRE(residency_data.empty());
}

TEST_CASE("ResidencyMetric: construction without inferred state", "[ResidencyMetric]") {
  // Create metric without inferred state
  astl::ResidencyMetricConfig config{"test_cpu_cstate_residency",
                                     "Unit test CPU C-state residency metric",
                                     ASTL_UNITS_TICKS,
                                     ASTL_VALUE_UINT64,
                                     ASTL_METRIC_RESIDENCY,
                                     ASTL_CATEGORY_UNCATEGORIZED,
                                     astl::CollectorType::SCMI,
                                     GetStatetoInfoMap()};
  astl::ResidencyMetric       metric{&config, CreateTestStateInfos(), nullptr, nullptr};

  // Verify initial state - no inferred state should be calculated
  auto summary = metric.GetResidencySummaryData();
  REQUIRE(summary.total_time_seconds.empty());
  REQUIRE(summary.average_percentage.empty());
  REQUIRE(!summary.inferred_state_percentage.has_value());

  auto residency_data = metric.GetResidencyData();
  REQUIRE(residency_data.empty());
}

TEST_CASE("ResidencyMetric: tick to seconds conversion", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  SECTION("Valid conversion") {
    astl::AstlValue                        ticks{uint64_t{1000000}};  // 1 million ticks
    astl::ResidencyMetricConfig::StateInfo config{"C0", 1000000.0,
                                                  astl::ScmiOperationBuilder{0x67DE}};  // 1MHz frequency
    auto result = ResidencyMetricTestFixture::TestConvertTicksToMicroseconds(ticks, config);

    REQUIRE(result.has_value());
    REQUIRE(result.value().count() == 1000000);  // 1000000 ticks / 1MHz = 1 second = 1,000,000 microseconds
  }

  SECTION("Invalid tick frequency") {
    astl::AstlValue                        ticks{uint64_t{1000000}};
    astl::ResidencyMetricConfig::StateInfo config{"InvalidState", 0.0,
                                                  astl::ScmiOperationBuilder{0x1234}};  // Invalid frequency
    auto result = ResidencyMetricTestFixture::TestConvertTicksToMicroseconds(ticks, config);

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }
}

TEST_CASE("ResidencyMetric: percentage calculation", "[ResidencyMetric]") {
  SECTION("Valid percentage calculation") {
    auto time_in_state  = std::chrono::microseconds{250000};   // 0.25 seconds = 250,000 microseconds
    auto total_interval = std::chrono::microseconds{1000000};  // 1 second = 1,000,000 microseconds

    auto result = ResidencyMetricTestFixture::TestCalculatePercentage(time_in_state, total_interval);

    REQUIRE(result.has_value());
    REQUIRE(result.value() == 25.0);  // 25% of the time
  }

  SECTION("100% residency") {
    auto time_in_state  = std::chrono::microseconds{1000000};  // 1 second = 1,000,000 microseconds
    auto total_interval = std::chrono::microseconds{1000000};  // 1 second = 1,000,000 microseconds

    auto result = ResidencyMetricTestFixture::TestCalculatePercentage(time_in_state, total_interval);

    REQUIRE(result.has_value());
    REQUIRE(result.value() == 100.0);
  }

  SECTION("Over 100% gets clamped") {
    auto time_in_state  = std::chrono::microseconds{2000000};  // 2 seconds = 2,000,000 microseconds
    auto total_interval = std::chrono::microseconds{1000000};  // 1 second = 1,000,000 microseconds

    auto result = ResidencyMetricTestFixture::TestCalculatePercentage(time_in_state, total_interval);

    REQUIRE(result.has_value());
    REQUIRE(result.value() == 100.0);  // Clamped to 100%
  }

  SECTION("Invalid interval") {
    auto time_in_state  = std::chrono::microseconds{500000};  // 0.5 seconds = 500,000 microseconds
    auto total_interval = std::chrono::microseconds{0};       // Invalid interval

    auto result = ResidencyMetricTestFixture::TestCalculatePercentage(time_in_state, total_interval);

    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }
}

TEST_CASE("ResidencyMetric: single sample processing", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(!operations.empty());

  // Get the first operation's ID (should correspond to C6 state, operation_id from config)
  auto first_operation_id = operations[0]->GetId();

  // First sample for C6 state - should store but not calculate residency
  astl::AstlValue      c6_val1{uint64_t{1000000}};  // 1 million ticks
  auto                 timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(std::chrono::steady_clock::now());
  astl::RawSampledData c6_sample1(first_operation_id, c6_val1, timestamp1);

  auto status = fixture.ReceiveRawSample(c6_sample1);
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  // No residency data should be generated yet
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.empty());
}

TEST_CASE("ResidencyMetric: two samples processing", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(!operations.empty());

  // Get the first operation's ID (should correspond to C6 state)
  auto first_operation_id = operations[0]->GetId();

  // First sample for C6 state
  astl::AstlValue      c6_val1{uint64_t{1000000}};  // 1 million ticks
  auto                 base_time  = std::chrono::steady_clock::now();
  auto                 timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(base_time);
  astl::RawSampledData c6_sample1(first_operation_id, c6_val1, timestamp1);
  fixture.ReceiveRawSample(c6_sample1);

  // Second sample for C6 state - 10ms later
  astl::AstlValue c6_val2{uint64_t{2000000}};  // 2 million ticks (delta = 1 million)
  auto            timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(10));
  astl::RawSampledData c6_sample2(first_operation_id, c6_val2, timestamp2);

  auto status = fixture.ReceiveRawSample(c6_sample2);
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  // Should have one residency data entry
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.size() == 1);

  const auto& data = residency_data[0];
  REQUIRE(data.state_name == "C6");
  REQUIRE(data.time_seconds == 1.0s);  // 1000000 ticks / 1000000 Hz = 1 second
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("ResidencyMetric: multiple states processing", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(operations.size() >= 2);

  // Get operation IDs for first two states (C6 and C1)
  auto first_operation_id  = operations[0]->GetId();  // C6 state
  auto second_operation_id = operations[1]->GetId();  // C1 state

  // C6 state samples - using explicit timestamps
  auto                 base_time = std::chrono::steady_clock::now();
  astl::AstlValue      c6_val1{uint64_t{1000000}};
  auto                 timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(base_time);
  astl::RawSampledData c6_sample1(first_operation_id, c6_val1, timestamp1);
  fixture.ReceiveRawSample(c6_sample1);

  astl::AstlValue c6_val2{uint64_t{2500000}};  // Delta: 1.5 million ticks = 1.5 seconds
  auto            timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(10));
  astl::RawSampledData c6_sample2(first_operation_id, c6_val2, timestamp2);
  fixture.ReceiveRawSample(c6_sample2);

  // C1 state samples
  astl::AstlValue c1_val1{uint64_t{500000}};
  auto            timestamp3 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(20));
  astl::RawSampledData c1_sample1(second_operation_id, c1_val1, timestamp3);
  fixture.ReceiveRawSample(c1_sample1);

  astl::AstlValue c1_val2{uint64_t{1000000}};  // Delta: 500000 ticks = 0.5 seconds
  auto            timestamp4 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(30));
  astl::RawSampledData c1_sample2(second_operation_id, c1_val2, timestamp4);
  fixture.ReceiveRawSample(c1_sample2);

  // Should have two residency data entries
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.size() == 2);

  // Find C6 and C1 data
  const astl::StateResidencyData* c6_data = nullptr;
  const astl::StateResidencyData* c1_data = nullptr;

  for (const auto& data : residency_data) {
    if (data.state_name == "C6") {
      c6_data = &data;
    } else if (data.state_name == "C1") {
      c1_data = &data;
    }
  }

  REQUIRE(c6_data != nullptr);
  REQUIRE(c1_data != nullptr);

  REQUIRE(c6_data->time_seconds == 1.5s);  // 1.5 seconds in C6
  REQUIRE(c1_data->time_seconds == 0.5s);  // 0.5 seconds in C1
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("ResidencyMetric: summarization", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(!operations.empty());

  // Get the first operation's ID (should correspond to C6 state)
  auto first_operation_id = operations[0]->GetId();

  // Generate simple data: only C6 state for simplicity
  auto                 base_timestamp = std::chrono::steady_clock::now();
  auto                 timestamp1     = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp);
  astl::RawSampledData c6_s1(first_operation_id, astl::AstlValue{uint64_t{1000000}}, timestamp1);  // 1M ticks baseline

  fixture.ReceiveRawSample(c6_s1);

  auto timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp + std::chrono::milliseconds(10));
  astl::RawSampledData c6_s2(first_operation_id, astl::AstlValue{uint64_t{2000000}},
                             timestamp2);  // 2M ticks (+1M = 1 second)
  fixture.ReceiveRawSample(c6_s2);

  // Summarize
  auto status = fixture.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  // Check that we have some data
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.size() == 1);
  REQUIRE(residency_data[0].state_name == "C6");
  REQUIRE(residency_data[0].time_seconds == 1.0s);

  // Check summary data
  auto summary = fixture.GetResidencySummaryData();
  REQUIRE(summary.total_time_seconds.contains("C6"));
  REQUIRE(summary.total_time_seconds.at("C6") == 1.0s);
  REQUIRE(summary.average_percentage.contains("C6"));

  // Inferred state should not be calculated if metric didnt receive samples for all states.
  REQUIRE(!summary.inferred_state_percentage.has_value());
  REQUIRE(!summary.inferred_state_time.has_value());
}

TEST_CASE("ResidencyMetric: invalid samples", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  SECTION("Invalid operation ID") {
    astl::AstlValue      invalid_val{uint64_t{1000000}};
    auto                 timestamp = ResidencyMetricTestFixture::CreateTimestamp(std::chrono::steady_clock::now());
    astl::RawSampledData invalid_sample(999, invalid_val, timestamp);  // operation_id 999 doesn't exist

    auto status = fixture.ReceiveRawSample(invalid_sample);
    REQUIRE(status == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }
}

TEST_CASE("ResidencyMetric: reset functionality", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(!operations.empty());

  // Get the first operation's ID
  auto first_operation_id = operations[0]->GetId();

  // Add some data
  auto                 base_timestamp = std::chrono::steady_clock::now();
  auto                 timestamp1     = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp);
  astl::RawSampledData sample1(first_operation_id, astl::AstlValue{uint64_t{1000000}}, timestamp1);

  fixture.ReceiveRawSample(sample1);

  auto timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp + std::chrono::milliseconds(10));
  astl::RawSampledData sample2(first_operation_id, astl::AstlValue{uint64_t{2000000}}, timestamp2);
  fixture.ReceiveRawSample(sample2);

  // Verify data exists
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.size() == 1);  // Should have one residency calculation

  // Reset
  fixture.Reset();

  // Verify data is cleared
  residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.empty());

  auto summary = fixture.GetResidencySummaryData();
  REQUIRE(summary.total_time_seconds.empty());
  REQUIRE(summary.average_percentage.empty());
  REQUIRE(!summary.inferred_state_percentage.has_value());
}

TEST_CASE("ResidencyMetric: state-specific data retrieval", "[ResidencyMetric]") {
  ResidencyMetricTestFixture fixture;

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(operations.size() >= 2);

  // Get operation IDs for first two states
  auto first_operation_id  = operations[0]->GetId();  // C6 state
  auto second_operation_id = operations[1]->GetId();  // C1 state

  // Add samples for both C6 and C1
  auto base_timestamp = std::chrono::steady_clock::now();
  auto timestamp1     = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp);
  fixture.ReceiveRawSample(astl::RawSampledData(first_operation_id, astl::AstlValue{uint64_t{1000000}}, timestamp1));
  auto timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp + std::chrono::milliseconds(10));
  fixture.ReceiveRawSample(astl::RawSampledData(first_operation_id, astl::AstlValue{uint64_t{2000000}}, timestamp2));

  auto timestamp3 = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp + std::chrono::milliseconds(10));
  fixture.ReceiveRawSample(astl::RawSampledData(second_operation_id, astl::AstlValue{uint64_t{500000}}, timestamp3));
  auto timestamp4 = ResidencyMetricTestFixture::CreateTimestamp(base_timestamp + std::chrono::milliseconds(20));
  fixture.ReceiveRawSample(astl::RawSampledData(second_operation_id, astl::AstlValue{uint64_t{1000000}}, timestamp4));

  // Get C6-specific data
  auto c6_data = fixture.GetStateResidencyData("C6");
  REQUIRE(c6_data.size() == 1);
  REQUIRE(c6_data[0].state_name == "C6");
  REQUIRE(c6_data[0].time_seconds == 1.0s);

  // Get C1-specific data
  auto c1_data = fixture.GetStateResidencyData("C1");
  REQUIRE(c1_data.size() == 1);
  REQUIRE(c1_data[0].state_name == "C1");
  REQUIRE(c1_data[0].time_seconds == 0.5s);

  // Get data for non-existent state
  auto invalid_data = fixture.GetStateResidencyData("NonExistentState");
  REQUIRE(invalid_data.empty());
}

TEST_CASE("ResidencyMetric: inferred state calculation setup", "[ResidencyMetric]") {
  // Use 1000 Hz frequency (1ms period) for easier calculations
  ResidencyMetricTestFixture fixture(1000.0);

  // Get the operations to find the actual operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations = operations_result.value();
  REQUIRE(operations.size() == 3);  // C6, C1, C2 states

  // Verify we can get operation IDs for all three states
  auto c6_operation_id = operations[0]->GetId();  // C6 state
  auto c1_operation_id = operations[1]->GetId();  // C1 state
  auto c2_operation_id = operations[2]->GetId();  // C2 state

  REQUIRE(c6_operation_id != c1_operation_id);
  REQUIRE(c1_operation_id != c2_operation_id);
  REQUIRE(c6_operation_id != c2_operation_id);
}

TEST_CASE("ResidencyMetric: inferred state baseline samples", "[ResidencyMetric]") {
  // Use 1000 Hz frequency (1ms period) for easier calculations
  ResidencyMetricTestFixture fixture(1000.0);

  // Get operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations      = operations_result.value();
  auto        c6_operation_id = operations[0]->GetId();
  auto        c1_operation_id = operations[1]->GetId();
  auto        c2_operation_id = operations[2]->GetId();

  // Create baseline samples
  auto base_time  = std::chrono::steady_clock::now();
  auto timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(base_time);

  // First samples for all states (baseline) - no residency calculation yet
  auto status1 =
      fixture.ReceiveRawSample(astl::RawSampledData(c6_operation_id, astl::AstlValue{uint64_t{1000}}, timestamp1));
  auto status2 =
      fixture.ReceiveRawSample(astl::RawSampledData(c1_operation_id, astl::AstlValue{uint64_t{500}}, timestamp1));
  auto status3 =
      fixture.ReceiveRawSample(astl::RawSampledData(c2_operation_id, astl::AstlValue{uint64_t{250}}, timestamp1));

  REQUIRE(status1 == ASTL_STATUS_SUCCESS);
  REQUIRE(status2 == ASTL_STATUS_SUCCESS);
  REQUIRE(status3 == ASTL_STATUS_SUCCESS);

  // No residency data should be generated yet (need deltas)
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.empty());
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("ResidencyMetric: inferred state calculation when all states receive samples", "[ResidencyMetric]") {
  // Use 1000 Hz frequency (1ms period) for easier calculations
  ResidencyMetricTestFixture fixture(1000.0);

  // Get operation IDs
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations      = operations_result.value();
  auto        c6_operation_id = operations[0]->GetId();
  auto        c1_operation_id = operations[1]->GetId();
  auto        c2_operation_id = operations[2]->GetId();

  // Send baseline samples
  auto base_time  = std::chrono::steady_clock::now();
  auto timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(base_time);
  fixture.ReceiveRawSample(astl::RawSampledData(c6_operation_id, astl::AstlValue{uint64_t{1000}}, timestamp1));
  fixture.ReceiveRawSample(astl::RawSampledData(c1_operation_id, astl::AstlValue{uint64_t{500}}, timestamp1));
  fixture.ReceiveRawSample(astl::RawSampledData(c2_operation_id, astl::AstlValue{uint64_t{250}}, timestamp1));

  // Wait and send second samples to trigger residency calculations
  auto timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(100));

  // C6: delta = 50 ticks = 0.05 seconds (50 ticks / 1000 Hz)
  fixture.ReceiveRawSample(astl::RawSampledData(c6_operation_id, astl::AstlValue{uint64_t{1050}}, timestamp2));

  // C1: delta = 25 ticks = 0.025 seconds (25 ticks / 1000 Hz)
  fixture.ReceiveRawSample(astl::RawSampledData(c1_operation_id, astl::AstlValue{uint64_t{525}}, timestamp2));

  // C2: delta = 15 ticks = 0.015 seconds (15 ticks / 1000 Hz)
  // After this sample, inferred state should be calculated since all states have been processed
  fixture.ReceiveRawSample(astl::RawSampledData(c2_operation_id, astl::AstlValue{uint64_t{265}}, timestamp2));

  // Verify residency data includes inferred state
  auto residency_data = fixture.GetResidencyData();
  REQUIRE(residency_data.size() == 4);  // C6, C1, C2, and Active (inferred state)

  // Find and verify inferred state data
  auto active_it = std::find_if(residency_data.begin(), residency_data.end(),
                                [](const auto& data) { return data.state_name == "Active"; });

  REQUIRE(active_it != residency_data.end());
  REQUIRE(active_it->state_name == "Active");
  REQUIRE(active_it->time_seconds >= 0.0s);
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("ResidencyMetric: inferred state summary verification", "[ResidencyMetric]") {
  // Use 1000 Hz frequency (1ms period) for easier calculations
  ResidencyMetricTestFixture fixture(1000.0);

  // Get operation IDs and set up samples (reusing setup logic)
  auto operations_result = fixture.GetOperations();
  REQUIRE(operations_result.has_value());
  const auto& operations      = operations_result.value();
  auto        c6_operation_id = operations[0]->GetId();
  auto        c1_operation_id = operations[1]->GetId();
  auto        c2_operation_id = operations[2]->GetId();

  // Create samples with known values
  auto base_time  = std::chrono::steady_clock::now();
  auto timestamp1 = ResidencyMetricTestFixture::CreateTimestamp(base_time);
  fixture.ReceiveRawSample(astl::RawSampledData(c6_operation_id, astl::AstlValue{uint64_t{1000}}, timestamp1));
  fixture.ReceiveRawSample(astl::RawSampledData(c1_operation_id, astl::AstlValue{uint64_t{500}}, timestamp1));
  fixture.ReceiveRawSample(astl::RawSampledData(c2_operation_id, astl::AstlValue{uint64_t{250}}, timestamp1));

  auto timestamp2 = ResidencyMetricTestFixture::CreateTimestamp(base_time + std::chrono::milliseconds(100));
  fixture.ReceiveRawSample(astl::RawSampledData(c6_operation_id, astl::AstlValue{uint64_t{1050}}, timestamp2));
  fixture.ReceiveRawSample(astl::RawSampledData(c1_operation_id, astl::AstlValue{uint64_t{525}}, timestamp2));
  fixture.ReceiveRawSample(astl::RawSampledData(c2_operation_id, astl::AstlValue{uint64_t{265}}, timestamp2));

  // Summarize and verify results
  auto status = fixture.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  auto summary = fixture.GetResidencySummaryData();

  // Verify all states are present in summary
  REQUIRE(summary.total_time_seconds.contains("C6"));
  REQUIRE(summary.total_time_seconds.contains("C1"));
  REQUIRE(summary.total_time_seconds.contains("C2"));
  REQUIRE(summary.total_time_seconds.contains("Active"));

  REQUIRE(summary.average_percentage.contains("C6"));
  REQUIRE(summary.average_percentage.contains("C1"));
  REQUIRE(summary.average_percentage.contains("C2"));
  REQUIRE(summary.average_percentage.contains("Active"));

  // Verify expected time values
  REQUIRE(summary.total_time_seconds.at("C6") == 50ms);
  REQUIRE(summary.total_time_seconds.at("C1") == 25ms);
  REQUIRE(summary.total_time_seconds.at("C2") == 15ms);

  // Calculate expected Active time: total_interval - accounted_time
  // Total interval = 100ms = 0.1s, Accounted = 0.05 + 0.025 + 0.015 = 0.09s
  // Expected Active time = 0.1 - 0.09 = 0.01s
  REQUIRE(summary.total_time_seconds.at("Active") == 10ms);

  // Verify Active state specific data
  auto active_state_data = fixture.GetStateResidencyData("Active");
  REQUIRE(active_state_data.size() == 1);
  REQUIRE(active_state_data[0].state_name == "Active");
}
