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

#ifndef RATE_METRIC_HPP_
#define RATE_METRIC_HPP_

#include <chrono>
#include <expected>
#include <optional>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "delta_metric.hpp"

namespace astl {

/**
 * @brief Holds rate calculation data between consecutive samples.
 * This structure stores the rate value and time interval information.
 */
struct RateData {
  double                    rate_value{0.0};   ///< The rate value (delta/time_interval)
  SampleTimestamp           timestamp;         ///< Timestamp when the rate was calculated
  std::chrono::microseconds time_interval{0};  ///< Time interval between samples in microseconds
};

/**
 * @brief Holds summary values for rate statistics.
 * This structure is used to store computed rate statistics.
 */
struct RateSummaryData {
  std::optional<double> min_rate;  ///< Minimum rate value seen
  std::optional<double> max_rate;  ///< Maximum rate value seen
  std::optional<double> avg_rate;  ///< Computed average of rate values
};

/**
 * @brief Rate metric class for handling accumulation-based metrics over time.
 *
 * RateMetric inherits from DeltaMetric and adds the ability to calculate rates
 * by dividing delta values by time intervals between consecutive samples.
 * It generates rate summaries and time interval outputs.
 *
 * Examples: power (watts = joules/sec), bandwidth (bytes/sec)
 */
class RateMetric : public DeltaMetric {
 public:
  RateMetric() = delete;
  /**
   * @brief Construct a RateMetric with specified name, description, units, and value type.
   *
   * Initializes the metric with the provided parameters and sets up initial rate tracking.
   * The rate units will be automatically derived as value_units/time.
   *
   * @param configuration The configuration for the metric, including name, units, and how to build operations
   * @param target The telemetry source for the metric.
   * @param processed_sample_sink Output for where processed samples should be sent.
   */
  explicit RateMetric(const MetricConfig *configuration, const ITarget *target,
                      IProcessedSampleSink *processed_sample_sink);

  /**
   * @brief Process and record a new sample value, calculating rate from delta and time.
   *
   * Inherits delta calculation from DeltaMetric and adds rate calculation by
   * dividing delta by time interval between samples.
   *
   * @param sample A single sampled data point to be processed.
   * @return astl_status_code indicating success or failure.
   */
  auto ReceiveRawSample(const RawSampledData &raw_sample) -> astl_status_code override;

  /**
   * @brief Summarize collected rate data.
   *
   * Finalizes the summary by calculating rate statistics.
   * Logs the results.
   *
   * @return astl_status_code indicating success or failure.
   */
  auto Summarize() -> astl_status_code override;

  /**
   * @brief Retrieve the rate summary data.
   *
   * Returns the current rate summary data containing minimum, maximum,
   * and average rate values.
   *
   * @return A RateSummaryData struct with rate statistics.
   */
  auto GetRateSummaryData() const -> RateSummaryData;

 protected:
  /**
   * @brief Calculate rate from delta value and time interval.
   *
   * @param delta_value The delta value between samples.
   * @param time_interval The time interval between samples in microseconds.
   * @return Expected rate value or error code.
   */
  static std::expected<AstlValue, astl_status_code> CalculateRate(const AstlValue          &delta_value,
                                                                  std::chrono::microseconds time_interval);

  /**
   * @brief Update rate statistics with a new rate value.
   *
   * @param rate_value The new rate value to incorporate.
   * @param timestamp The timestamp when the rate was calculated.
   * @param time_interval The time interval for this rate calculation.
   * @return astl_status_code indicating success or failure.
   */
  auto UpdateRateStatistics(const AstlValue &rate_value) -> astl_status_code;

 private:
  RateSummaryData _rate_summary_data;    // Summary data for rate statistics
  double          _sum_rate_value{0.0};  // Sum of rate values for average calculation
  uint64_t        _rate_count{0};        // Count of rates processed
  astl::Logger    _interval_logger;
};  // End of RateMetric class

}  // namespace astl

#endif  // RATE_METRIC_HPP_
