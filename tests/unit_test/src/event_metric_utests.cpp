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
#include <unordered_map>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "metric/event_metric.hpp"

using namespace std::chrono_literals;

static auto GetEventMetricConfig() -> const astl::MetricConfig* {
  static astl::MetricConfig config{"test_event",
                                   "Unit test event metric",
                                   ASTL_UNITS_NONE,
                                   ASTL_VALUE_STRING,
                                   ASTL_CATEGORY_UNCATEGORIZED,
                                   ASTL_METRIC_EVENT,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return &config;
}

static auto CreateEventMetricWithSink(MockSampleSink* sink) -> astl::EventMetric {
  return astl::EventMetric(GetEventMetricConfig(), nullptr, sink);
}

// Test fixture class to access protected members
class EventMetricTestFixture : public astl::EventMetric {
 public:
  explicit EventMetricTestFixture(MockSampleSink* sink) : astl::EventMetric(GetEventMetricConfig(), nullptr, sink) {}

  // Helper method to inject events for testing
  astl_status_code InjectEvent(const std::string& event_description) {
    astl::RawSampledData sample(0, astl::AstlValue{std::string{event_description}},
                                CreateTimestamp(std::chrono::steady_clock::now()));
    return ReceiveRawSample(sample);
  }
  // Helper method to create properly typed timestamps
  static astl::SampleTimestamp CreateTimestamp(std::chrono::steady_clock::time_point timePoint) {
    return std::chrono::time_point_cast<astl::SampleTimestamp::duration>(timePoint);
  }
};

// Helper function to extract event strings from processed samples
static std::vector<std::string> ExtractEventStrings(const std::vector<astl::ProcessedSampledData>& samples) {
  std::vector<std::string> events;
  for (const auto& sample : samples) {
    std::string event_str;
    if (sample.value.ToStringValue(event_str)) {
      events.push_back(event_str);
    }
  }
  return events;
}

// Helper function to count event occurrences
static std::unordered_map<std::string, size_t> CountEvents(const std::vector<astl::ProcessedSampledData>& samples) {
  std::unordered_map<std::string, size_t> counts;
  for (const auto& sample : samples) {
    std::string event_str;
    if (sample.value.ToStringValue(event_str)) {
      counts[event_str]++;
    }
  }
  return counts;
}

TEST_CASE("EventMetric: construction", "[EventMetric]") {
  MockSampleSink    sink;
  astl::EventMetric metric = CreateEventMetricWithSink(&sink);

  // Verify initial state

  auto summary = metric.GetEventSummaryData();
  REQUIRE(summary.counts.empty());

  REQUIRE(sink.captured.empty());
}

TEST_CASE("EventMetric: single event capture", "[EventMetric]") {
  MockSampleSink         sink;
  EventMetricTestFixture metric(&sink);

  // Test single event injection
  REQUIRE(metric.InjectEvent("INTERRUPT_WAKEUP") == ASTL_STATUS_SUCCESS);

  auto events = ExtractEventStrings(sink.captured);
  REQUIRE(events.size() == 1);
  REQUIRE(events[0] == "INTERRUPT_WAKEUP");
}

TEST_CASE("EventMetric: multiple event capture", "[EventMetric]") {
  MockSampleSink         sink;
  EventMetricTestFixture metric(&sink);

  // Test multiple event injection
  REQUIRE(metric.InjectEvent("CPU_IDLE_TO_ACTIVE") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("DROOP_EVENT") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("CPU_IDLE_TO_ACTIVE") == ASTL_STATUS_SUCCESS);  // Duplicate event

  // Check event timeline
  auto events = ExtractEventStrings(sink.captured);
  REQUIRE(events.size() == 3);
  REQUIRE(events[0] == "CPU_IDLE_TO_ACTIVE");
  REQUIRE(events[1] == "DROOP_EVENT");
  REQUIRE(events[2] == "CPU_IDLE_TO_ACTIVE");

  // Check event counts
  auto counts = CountEvents(sink.captured);
  REQUIRE(counts.at("CPU_IDLE_TO_ACTIVE") == 2);
  REQUIRE(counts.at("DROOP_EVENT") == 1);
}

TEST_CASE("EventMetric: numeric sample type conversion", "[EventMetric]") {
  MockSampleSink    sink;
  astl::EventMetric metric = CreateEventMetricWithSink(&sink);

  // Test acceptance and conversion of numeric samples to strings
  auto                 timestamp = EventMetricTestFixture::CreateTimestamp(std::chrono::steady_clock::now());
  astl::RawSampledData uint64_sample(0, astl::AstlValue{uint64_t{42}}, timestamp);
  astl::RawSampledData bool_sample(1, astl::AstlValue{true}, timestamp);
  astl::RawSampledData float_sample(2, astl::AstlValue{3.14F}, timestamp);

  // These should now succeed with automatic conversion
  REQUIRE(metric.ReceiveRawSample(uint64_sample) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(bool_sample) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(float_sample) == ASTL_STATUS_SUCCESS);

  // Verify events were stored with string representations
  auto events = ExtractEventStrings(sink.captured);
  REQUIRE(events.size() == 3);
  REQUIRE(events[0] == "42");
  REQUIRE(events[1] == "true");
  REQUIRE(events[2] == "3.140000");  // float to string representation

  auto counts = CountEvents(sink.captured);
  REQUIRE(counts.at("42") == 1);
  REQUIRE(counts.at("true") == 1);
  REQUIRE(counts.at("3.140000") == 1);
}

TEST_CASE("EventMetric: reset functionality", "[EventMetric]") {
  MockSampleSink         sink;
  EventMetricTestFixture metric(&sink);

  // Add some events
  REQUIRE(metric.InjectEvent("EVENT1") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("EVENT2") == ASTL_STATUS_SUCCESS);

  // Verify they exist
  REQUIRE(sink.captured.size() == 2);

  // Reset and verify empty
  metric.Reset();
  REQUIRE(metric.GetEventSummaryData().counts.empty());
}

TEST_CASE("EventMetric: summarize functionality", "[EventMetric]") {
  MockSampleSink         sink;
  EventMetricTestFixture metric(&sink);

  // Add some events
  REQUIRE(metric.InjectEvent("STATE_TRANSITION") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("POWER_EVENT") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("STATE_TRANSITION") == ASTL_STATUS_SUCCESS);

  // Summarize should succeed and log results
  REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);

  // Verify captured events are still accessible
  auto counts = CountEvents(sink.captured);
  REQUIRE(counts.at("STATE_TRANSITION") == 2);
  REQUIRE(counts.at("POWER_EVENT") == 1);
}

TEST_CASE("EventMetric: event timestamp ordering", "[EventMetric]") {
  MockSampleSink    sink;
  astl::EventMetric metric = CreateEventMetricWithSink(&sink);

  auto start_time       = std::chrono::steady_clock::now();
  auto first_timestamp  = EventMetricTestFixture::CreateTimestamp(start_time);
  auto second_timestamp = EventMetricTestFixture::CreateTimestamp(start_time + 100ms);

  // Create samples with explicit timestamps
  astl::RawSampledData first_sample(0, astl::AstlValue{std::string{"FIRST_EVENT"}}, first_timestamp);
  astl::RawSampledData second_sample(0, astl::AstlValue{std::string{"SECOND_EVENT"}}, second_timestamp);

  REQUIRE(metric.ReceiveRawSample(first_sample) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(second_sample) == ASTL_STATUS_SUCCESS);

  auto events = ExtractEventStrings(sink.captured);
  REQUIRE(events.size() == 2);
  REQUIRE(events[0] == "FIRST_EVENT");
  REQUIRE(events[1] == "SECOND_EVENT");

  // Verify timestamp ordering
  REQUIRE(sink.captured[0].timestamp < sink.captured[1].timestamp);
}
