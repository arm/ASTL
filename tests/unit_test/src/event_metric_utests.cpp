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
#include <thread>

#include "../../test_includes.hpp"  // include before catch2
#include "metric/event_metric.hpp"

using namespace std::chrono_literals;

// Test fixture class to access protected members
class EventMetricTestFixture : public astl::EventMetric {
 public:
  EventMetricTestFixture() : astl::EventMetric("test_event_metric", "Unit test event metric") {}

  // Helper method to inject events for testing
  astl_status_code InjectEvent(const std::string& event_description) {
    astl::SampledData sample(0, astl::AstlValue{std::string{event_description}},
                             CreateTimestamp(std::chrono::steady_clock::now()));
    return ReceiveSample(sample);
  }
  // Helper method to create properly typed timestamps
  static astl::SampleTimestamp CreateTimestamp(std::chrono::steady_clock::time_point timePoint) {
    return std::chrono::time_point_cast<astl::SampleTimestamp::duration>(timePoint);
  }
};

TEST_CASE("EventMetric: construction", "[EventMetric]") {
  astl::EventMetric metric("test_event", "Unit test event metric");

  // Verify initial state
  auto events = metric.GetEvents();
  REQUIRE(events.empty());

  auto summary = metric.GetEventSummaryData();
  REQUIRE(summary.counts.empty());

  auto samples = metric.GetSamples();
  REQUIRE(samples.empty());
}

TEST_CASE("EventMetric: single event capture", "[EventMetric]") {
  EventMetricTestFixture metric;
  // Test single event injection
  REQUIRE(metric.InjectEvent("INTERRUPT_WAKEUP") == ASTL_STATUS_SUCCESS);

  auto events = metric.GetEvents();
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].description == "INTERRUPT_WAKEUP");
}

TEST_CASE("EventMetric: multiple event capture", "[EventMetric]") {
  EventMetricTestFixture metric;

  // Test multiple event injection
  REQUIRE(metric.InjectEvent("CPU_IDLE_TO_ACTIVE") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("DROOP_EVENT") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("CPU_IDLE_TO_ACTIVE") == ASTL_STATUS_SUCCESS);  // Duplicate event

  // Check event timeline
  auto events = metric.GetEvents();
  REQUIRE(events.size() == 3);
  REQUIRE(events[0].description == "CPU_IDLE_TO_ACTIVE");
  REQUIRE(events[1].description == "DROOP_EVENT");
  REQUIRE(events[2].description == "CPU_IDLE_TO_ACTIVE");

  // Check event counts
  auto summary = metric.GetEventSummaryData();
  REQUIRE(summary.counts.at("CPU_IDLE_TO_ACTIVE") == 2);
  REQUIRE(summary.counts.at("DROOP_EVENT") == 1);
}

TEST_CASE("EventMetric: numeric sample type conversion", "[EventMetric]") {
  astl::EventMetric metric("test_event", "Unit test event metric");

  // Test acceptance and conversion of numeric samples to strings
  auto              timestamp = EventMetricTestFixture::CreateTimestamp(std::chrono::steady_clock::now());
  astl::SampledData uint64_sample(0, astl::AstlValue{uint64_t{42}}, timestamp);
  astl::SampledData bool_sample(1, astl::AstlValue{true}, timestamp);
  astl::SampledData float_sample(2, astl::AstlValue{3.14F}, timestamp);

  // These should now succeed with automatic conversion
  REQUIRE(metric.ReceiveSample(uint64_sample) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveSample(bool_sample) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveSample(float_sample) == ASTL_STATUS_SUCCESS);

  // Verify events were stored with string representations
  auto events = metric.GetEvents();
  REQUIRE(events.size() == 3);
  REQUIRE(events[0].description == "42");
  REQUIRE(events[1].description == "true");
  REQUIRE(events[2].description == "3.140000");  // float to string representation

  auto summary = metric.GetEventSummaryData();
  REQUIRE(summary.counts.at("42") == 1);
  REQUIRE(summary.counts.at("true") == 1);
  REQUIRE(summary.counts.at("3.140000") == 1);
}

TEST_CASE("EventMetric: reset functionality", "[EventMetric]") {
  EventMetricTestFixture metric;

  // Add some events
  REQUIRE(metric.InjectEvent("EVENT1") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("EVENT2") == ASTL_STATUS_SUCCESS);

  // Verify they exist
  REQUIRE(metric.GetEvents().size() == 2);
  REQUIRE(metric.GetEventSummaryData().counts.size() == 2);

  // Reset and verify empty
  metric.Reset();
  REQUIRE(metric.GetEvents().empty());
  REQUIRE(metric.GetEventSummaryData().counts.empty());
}

TEST_CASE("EventMetric: summarize functionality", "[EventMetric]") {
  EventMetricTestFixture metric;

  // Add some events
  REQUIRE(metric.InjectEvent("STATE_TRANSITION") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("POWER_EVENT") == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.InjectEvent("STATE_TRANSITION") == ASTL_STATUS_SUCCESS);

  // Summarize should succeed and log results
  REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);

  // Verify summary data is still accessible
  auto summary = metric.GetEventSummaryData();
  REQUIRE(summary.counts.at("STATE_TRANSITION") == 2);
  REQUIRE(summary.counts.at("POWER_EVENT") == 1);
}

TEST_CASE("EventMetric: event timestamp ordering", "[EventMetric]") {
  astl::EventMetric metric("test_event", "Unit test event metric");

  auto start_time       = std::chrono::steady_clock::now();
  auto first_timestamp  = EventMetricTestFixture::CreateTimestamp(start_time);
  auto second_timestamp = EventMetricTestFixture::CreateTimestamp(start_time + 100ms);

  // Create samples with explicit timestamps
  astl::SampledData first_sample(0, astl::AstlValue{std::string{"FIRST_EVENT"}}, first_timestamp);
  astl::SampledData second_sample(0, astl::AstlValue{std::string{"SECOND_EVENT"}}, second_timestamp);

  REQUIRE(metric.ReceiveSample(first_sample) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveSample(second_sample) == ASTL_STATUS_SUCCESS);

  auto events = metric.GetEvents();
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].description == "FIRST_EVENT");
  REQUIRE(events[1].description == "SECOND_EVENT");

  // Verify timestamp ordering
  REQUIRE(events[0].timestamp < events[1].timestamp);
}
