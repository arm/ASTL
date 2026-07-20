// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef EVENT_METRIC_HPP_
#define EVENT_METRIC_HPP_

#include <string>
#include <unordered_map>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "raw_metric.hpp"

namespace astl {

/**
 * @brief Represents a single captured event with its timestamp.
 */
struct EventData {
  std::string              description;  ///< Event textual description/value
  ProcessedSampleTimestamp timestamp;    ///< Time when event occurred
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
   * @param configuration The non-owned pointer to configuration for the metric, including name, units, and how to build
   * operations
   * @param target The telemetry source for the metric.
   * @param processed_sample_sink Output for where processed samples should be sent.
   */
  explicit EventMetric(const MetricConfig* configuration, const ITarget* target,
                       IProcessedSampleSink* processed_sample_sink)
      : RawMetric(configuration, target, processed_sample_sink) {
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
  auto ReceiveRawSample(const NormalizedSampledData& sample) -> astl_status_code override;

  /**
   * @brief Summarize collected event data.
   *
   * Logs the final event counts to the summary log file. The summary contains
   * the total count of occurrences for each unique event description.
   *
   * @return astl_status_code indicating success or failure.
   * @retval ASTL_STATUS_SUCCESS Summary was successfully generated and logged.
   */
  auto Summarize() -> astl_status_code override;

  /**
   * @brief Reset the metric state, clearing all collected events and counts.
   *
   * Removes all stored events and resets event counts to zero. The metric
   * returns to its initial state as if no events had been received.
   */
  void Reset() override;

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
  auto CheckAndStoreEvent(const NormalizedSampledData& raw_sample) -> astl_status_code;

  /**
   * @brief Initialize/reset internal data structures.
   *
   * Clears all event data and resets the summary counts to prepare
   * the metric for fresh data collection.
   */
  void Initialize();

  EventSummaryData _summary;  ///< Aggregated counts

  Logger _event_summary_logger{LogLevel::Info, false, false, "event_summary.log"};    ///< Logger for event counts
  Logger _event_timeline_logger{LogLevel::Info, false, false, "event_timeline.log"};  ///< Logger for event timeline
};

}  // namespace astl

#endif  // EVENT_METRIC_HPP_
