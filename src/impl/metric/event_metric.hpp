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

#ifndef EVENT_METRIC_HPP_
#define EVENT_METRIC_HPP_

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "raw_metric.hpp"

namespace astl {

/**
 * @brief Represents a single captured event with its timestamp.
 */
struct EventData {
  std::string     description;  ///< Event textual description/value
  SampleTimestamp timestamp;    ///< Time when event occurred
};

/**
 * @brief Summary data for event metrics (counts per unique event string).
 */
struct EventSummaryData {
  std::unordered_map<std::string, size_t> counts;  ///< Occurrence count per event description
};

/**
 * @brief Metric type specialized for discrete events (string valued samples).
 *
 * EventMetric handles events that occur when specific conditions are met.
 * Examples include:
 * - State transitions (e.g., "CPU_IDLE_TO_ACTIVE", "GPU_POWER_DOWN")
 * - System wakeup events (e.g., "INTERRUPT_WAKEUP", "TIMER_WAKEUP")
 * - Error conditions (e.g., "THERMAL_THROTTLE", "MEMORY_ERROR")
 *
 * - Record each event with its timestamp for timeline analysis
 * - Summarize by counting occurrences of each unique event description
 *
 * Time-based interval output provides a simple chronological list of
 * (timestamp, description) pairs showing when each event occurred.
 */
class EventMetric : public RawMetric {
 public:
  EventMetric() = delete;

  /**
   * @brief Construct an EventMetric with specified name and description.
   *
   * Initializes the metric with the provided parameters and sets up event tracking.
   * The metric will capture and count discrete string-valued events.
   *
   * @param name The name of the metric (e.g., "system_events").
   * @param description A brief description of the metric.
   */
  explicit EventMetric(const char* name, const char* description, const ITarget* target,
                       IProcessedSampleSink* processed_sample_sink)
      : RawMetric(name, description, ASTL_UNITS_NONE, ASTL_VALUE_STRING, ASTL_METRIC_EVENT, target,
                  processed_sample_sink) {
    // Summary: Metric, Event, Count
    _event_summary_logger.LogInfo("Metric, Event, Count\n");
    // Timeline: Metric, Event, Timestamp(µs)
    _event_timeline_logger.LogInfo("Metric, Event, Timestamp(µs)\n");
  }

  /**
   * @brief Process and record a new event sample.
   *
   * Validates that the sample contains a string value, extracts the event description,
   * updates event counts, and logs the event to the timeline.
   *
   * @param sample A single sampled data point containing a string event description.
   * @return astl_status_code indicating success or failure.
   * @retval ASTL_STATUS_SUCCESS Event was successfully processed and recorded.
   * @retval ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE Sample does not contain a string convertible value.
   */
  astl_status_code ReceiveRawSample(const RawSampledData& sample) override;

  /**
   * @brief Summarize collected event data.
   *
   * Logs the final event counts to the summary log file. The summary contains
   * the total count of occurrences for each unique event description.
   *
   * @return astl_status_code indicating success or failure.
   * @retval ASTL_STATUS_SUCCESS Summary was successfully generated and logged.
   */
  astl_status_code Summarize() override;

  /**
   * @brief Reset the metric state, clearing all collected events and counts.
   *
   * Removes all stored events and resets event counts to zero. The metric
   * returns to its initial state as if no events had been received.
   */
  void Reset() override;

  /**
   * @brief Get raw sample data (not implemented for EventMetric).
   *
   * EventMetric does not store raw samples as they are immediately processed
   * into structured EventData objects. This method currently returns an empty span.
   *
   * @note @todo (ASTL-159): Return the EventData samples in GetProcessedSamples API
   *
   * @return Empty span since raw samples are not returned.
   */
  std::span<const ProcessedSampledData> GetProcessedSamples() const override { return {}; }

  /**
   * @brief Get a view of all captured events in chronological order.
   *
   * Returns a span containing all EventData objects, providing access to
   * the complete timeline of events with their descriptions and timestamps.
   * @note @todo (ASTL-159): Remove this API when GetProcessedSamples API returns EventData
   *
   * @return Span of EventData objects in the order they were received.
   */
  std::span<const EventData> GetEvents() const { return _events; }

  /**
   * @brief Retrieve the event summary data containing occurrence counts.
   *
   * Returns the current event summary data with counts for each unique
   * event description that has been received by this metric.
   *
   * @return A const reference to EventSummaryData containing event counts.
   */
  const EventSummaryData& GetEventSummaryData() const { return _summary; }

 private:
  /**
   * @brief Validate and store an event from a sample.
   *
   * Internal method that validates the sample contains a string value,
   * extracts the event description, stores it in the events timeline,
   * updates occurrence counts, and logs the event.
   *
   * @param sample The sample data containing the event information.
   * @return astl_status_code indicating success or validation failure.
   */
  astl_status_code CheckAndStoreEvent(const RawSampledData& raw_sample);

  /**
   * @brief Initialize/reset internal data structures.
   *
   * Clears all event data and resets the summary counts to prepare
   * the metric for fresh data collection.
   */
  void Initialize();

  std::vector<EventData> _events;   ///< Flattened event timeline
  EventSummaryData       _summary;  ///< Aggregated counts

  Logger _event_summary_logger{LogLevel::Info, false, false, "event_summary.log"};    ///< Logger for event counts
  Logger _event_timeline_logger{LogLevel::Info, false, false, "event_timeline.log"};  ///< Logger for event timeline
};

}  // namespace astl

#endif  // EVENT_METRIC_HPP_
