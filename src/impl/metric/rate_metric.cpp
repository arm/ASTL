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

#include "rate_metric.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <optional>
#include <string>

#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace {
inline auto RateSummaryLogger() -> astl::Logger& {
  static astl::Logger logger(astl::LogLevel::Info, false, false, "rate_summary.log");
  static bool         header = []() {
    logger.LogInfo("Metric, Description, Units, Min Rate, Max Rate, Avg Rate, Rate Count, Type \n");
    return true;
  }();
  (void)header;
  return logger;
}
}  // anonymous namespace

namespace astl {

RateMetric::RateMetric(const MetricConfig* configuration, const ITarget* target,
                       IProcessedSampleSink* processed_sample_sink)
    : DeltaMetric(configuration, target, processed_sample_sink),
      _rate_summary_data{},
      _interval_logger(astl::LogLevel::Info, false, false,
                       RawMetric::SanitizeMetricNameForFilename(_configuration->Name()) + "_rate_intervals.csv") {
  // Initialize rate summary data - rates are always double
  astl_value_t val{0};
  auto         zero_val = AstlValue::FromUnion(val, _configuration->ValueType());
  if (zero_val.has_value()) {
    _rate_summary_data = {.min_rate = std::nullopt, .max_rate = std::nullopt, .avg_rate = std::nullopt};
  } else {
    ASTL_LOG_INFO("RateMetric: unsupported type {} for rate statistics for metric: {}", _configuration->ValueType(),
                  _configuration->Name());
  }

  // Header initialization of rate loggers
  _interval_logger.LogInfo("Metric, Rate Value, Time Interval (us), Timestamp \n");
}

auto RateMetric::ReceiveRawSample(const RawSampledData& raw_sample) -> astl_status_code {
  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(raw_sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Store the previous sample's timestamp before calling base class
  // (the base class will update _previous_sample at the end)
  std::optional<SampleTimestamp> previous_timestamp;
  if (_previous_sample.has_value()) {
    previous_timestamp = _previous_sample->timestamp;
  }

  // First, let the base DeltaMetric process the sample and calculate delta
  auto delta_status = DeltaMetric::ReceiveRawSample(raw_sample);
  if (delta_status != ASTL_STATUS_SUCCESS) {
    return delta_status;
  }

  // If we don't have a previous timestamp (first sample) or no deltas yet, we can't calculate rate
  if (!previous_timestamp.has_value() || _deltas.empty()) {
    return ASTL_STATUS_SUCCESS;
  }

  // Get the latest delta and calculate rate
  const auto& delta_data = _deltas.back();

  // Calculate time interval between samples
  auto time_interval = raw_sample.timestamp - previous_timestamp.value();

  // Prevent division by zero
  if (time_interval.count() == 0) {
    ASTL_LOG_ERROR("RateMetric: zero time interval detected for metric {}, skipping rate calculation",
                   _configuration->Name());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  // Calculate rate: delta / time_interval
  auto rate_result = CalculateRate(delta_data.value, time_interval);
  if (!rate_result.has_value()) {
    ASTL_LOG_ERROR("RateMetric: failed to calculate rate for metric {}: {}", _configuration->Name(),
                   astlStatusString(rate_result.error()));
    return rate_result.error();
  }

  // Update rate statistics
  auto rate_status = UpdateRateStatistics(rate_result.value(), raw_sample.timestamp, time_interval);
  if (rate_status != ASTL_STATUS_SUCCESS) {
    return rate_status;
  }

  return ASTL_STATUS_SUCCESS;
}

auto RateMetric::CalculateRate(const AstlValue& delta_value, std::chrono::microseconds time_interval)
    -> std::expected<AstlValue, astl_status_code> {
  if (!delta_value.IsArithmetic()) {
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }

  // Convert time interval to seconds (as double) for rate calculation
  auto time_in_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(time_interval).count();

  // Calculate rate: delta / time using the template Divide method
  auto rate_result = AstlValue::Divide(delta_value, time_in_seconds);
  if (!rate_result.has_value()) {
    return std::unexpected(rate_result.error());
  }

  return rate_result.value();
}

auto RateMetric::UpdateRateStatistics(const AstlValue& rate_value, SampleTimestamp timestamp,
                                      std::chrono::microseconds time_interval) -> astl_status_code {
  if (!rate_value.IsArithmetic()) {
    ASTL_LOG_TRACE("RateMetric: received rate with non-arithmetic value type for metric: {}", _configuration->Name());
    return ASTL_STATUS_SUCCESS;
  }

  // Extract the double value from the rate_value (which should be double since it's divided by time)
  double rate_value_in_double = std::get<double>(rate_value.value);

  // Update min and max rate values
  _rate_summary_data.min_rate = _rate_summary_data.min_rate.has_value()
                                    ? std::min(_rate_summary_data.min_rate.value(), rate_value_in_double)
                                    : rate_value_in_double;
  _rate_summary_data.max_rate = _rate_summary_data.max_rate.has_value()
                                    ? std::max(_rate_summary_data.max_rate.value(), rate_value_in_double)
                                    : rate_value_in_double;

  // Store rate data for analysis
  _rates.push_back({rate_value_in_double, timestamp, time_interval});

  ProcessedSampledData processed_sampled_data{static_cast<AstlValue>(rate_value_in_double), timestamp};

  SinkProcessedSample(processed_sampled_data);

  // Update sum for average calculation
  _sum_rate_value += rate_value_in_double;

  return ASTL_STATUS_SUCCESS;
}

auto RateMetric::Summarize() -> astl_status_code {
  // First, let the base DeltaMetric summarize
  auto delta_status = DeltaMetric::Summarize();
  if (delta_status != ASTL_STATUS_SUCCESS) {
    return delta_status;
  }

  // Compute rate statistics
  if (_rates.empty()) {
    RateSummaryLogger().LogInfo("No rates to summarize for metric: {}.", _configuration->Name());
  } else {
    // Calculate average rate
    _rate_summary_data.avg_rate = _sum_rate_value / static_cast<double>(_rates.size());

    // Output time interval packets
    auto interval_status = OutputTimeIntervalPackets();
    if (interval_status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to output time interval packets for metric {}", _configuration->Name());
    }

    // Log rate summary
    RateSummaryLogger().LogInfo("{}, {}, {}, {}, {}, {}, {}, {} \n", _configuration->Name(),
                                _configuration->Description(), _configuration->Units(),
                                _rate_summary_data.min_rate.value_or(-1.0), _rate_summary_data.max_rate.value_or(-1.0),
                                _rate_summary_data.avg_rate.value_or(-1.0), _rates.size(), _configuration->ValueType());
  }

  return ASTL_STATUS_SUCCESS;
}

auto RateMetric::OutputTimeIntervalPackets() -> astl_status_code {
  _interval_logger.LogInfo("Time Interval Packets for metric: {} \n", _configuration->Name());

  for (const auto& rate_data : _rates) {
    _interval_logger.LogInfo("{}, {}, {}, {} \n", _configuration->Name(), rate_data.rate_value,
                             rate_data.time_interval.count(), rate_data.timestamp.time_since_epoch().count());
  }

  return ASTL_STATUS_SUCCESS;
}

auto RateMetric::GetRateSummaryData() const -> RateSummaryData { return _rate_summary_data; }

/**
 * @brief Return a view of the samples processed by this metric
 */
auto RateMetric::GetRates() const -> std::span<const RateData> { return std::span<const RateData>(_rates); }

}  // namespace astl
