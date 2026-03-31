// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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

auto RateMetric::ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code {
  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(raw_sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the normalized sample using the base class method
  LogNormalizedSample(raw_sample);

  // Apply formula if configured (masking, scaling, etc.)
  auto processed_value = ApplyFormula(raw_sample.value);
  if (!processed_value) {
    ASTL_LOG_ERROR("RateMetric: failed to apply formula for metric: {}, error: {}", _configuration->Name(),
                   astlStatusString(processed_value.error()));
    return processed_value.error();
  }

  // If this is the first sample, store it and return
  if (!_previous_sample.has_value()) {
    // Store the processed value for next rate calculation
    _previous_sample = NormalizedSampledData{raw_sample.operation_id, *processed_value, raw_sample.timestamp};
    return ASTL_STATUS_SUCCESS;
  }

  // Calculate delta between current and previous sample (both processed)
  auto delta_result = DeltaMetric::CalculateDelta(*processed_value, _previous_sample->value);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR("RateMetric: failed to calculate delta for metric {}: {}", _configuration->Name(),
                   astlStatusString(delta_result.error()));
    return delta_result.error();
  }

  // Calculate time interval between samples (nanoseconds → cast to microseconds for rate calculation)
  auto time_interval_ns = raw_sample.timestamp - _previous_sample->timestamp;
  auto time_interval    = std::chrono::duration_cast<std::chrono::microseconds>(time_interval_ns);

  // Prevent division by zero
  if (time_interval.count() == 0) {
    ASTL_LOG_ERROR("RateMetric: zero time interval detected for metric {}, skipping rate calculation",
                   _configuration->Name());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  // Calculate rate: delta / time_interval
  auto rate_result = CalculateRate(delta_result.value(), time_interval);
  if (!rate_result.has_value()) {
    ASTL_LOG_ERROR("RateMetric: failed to calculate rate for metric {}: {}", _configuration->Name(),
                   astlStatusString(rate_result.error()));
    return rate_result.error();
  }

  // Forward the rate as a processed sample to the sink
  ProcessedSampledData processed_sampled_data{rate_result.value(), raw_sample.timestamp};
  SinkProcessedSample(processed_sampled_data);

  // Log the rate value, time interval, and timestamp
  _interval_logger.LogInfo("{}, {}, {}, {}", _configuration->Name(), to_string(rate_result.value()),
                           time_interval.count(), raw_sample.timestamp.time_since_epoch().count());

  // Update rate statistics
  auto rate_status = UpdateRateStatistics(rate_result.value());
  if (rate_status != ASTL_STATUS_SUCCESS) {
    return rate_status;
  }

  // Store current processed sample as previous for next iteration
  _previous_sample = NormalizedSampledData{raw_sample.operation_id, *processed_value, raw_sample.timestamp};

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

auto RateMetric::UpdateRateStatistics(const AstlValue& rate_value) -> astl_status_code {
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

  // Update sum and count for average calculation
  _sum_rate_value += rate_value_in_double;
  ++_rate_count;

  return ASTL_STATUS_SUCCESS;
}

auto RateMetric::Summarize() -> astl_status_code {
  // Compute rate statistics
  if (_rate_count == 0) {
    RateSummaryLogger().LogInfo("No rates to summarize for metric: {}.", _configuration->Name());
  } else {
    // Calculate average rate
    _rate_summary_data.avg_rate = _sum_rate_value / static_cast<double>(_rate_count);

    // Log rate summary
    RateSummaryLogger().LogInfo("{}, {}, {}, {}, {}, {}, {}, {} \n", _configuration->Name(),
                                _configuration->Description(), _configuration->Units(),
                                _rate_summary_data.min_rate.value_or(-1.0), _rate_summary_data.max_rate.value_or(-1.0),
                                _rate_summary_data.avg_rate.value_or(-1.0), _rate_count, _configuration->ValueType());
  }

  return ASTL_STATUS_SUCCESS;
}

auto RateMetric::GetRateSummaryData() const -> RateSummaryData { return _rate_summary_data; }

}  // namespace astl
