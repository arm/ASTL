// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "delta_metric.hpp"

#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace astl {

DeltaMetric::DeltaMetric(const MetricConfig* configuration, const ITarget* target,
                         IProcessedSampleSink* processed_sample_sink)
    : RawMetric(configuration, target, processed_sample_sink),
      _previous_sample{std::nullopt},
      _delta_summary_data{},
      _sum_delta_value{uint64_t{0}} {
  InitializeSamples();

  // Header initialization for delta summary logger
  _delta_summary_logger.LogInfo("Metric, Description, Units, Delta Value \n");
}

auto DeltaMetric::ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code {
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
    ASTL_LOG_ERROR("DeltaMetric: failed to apply formula for metric: {}, error: {}", _configuration->Name(),
                   astlStatusString(processed_value.error()));
    return processed_value.error();
  }

  // If this is the first sample, store it and return
  if (!_previous_sample.has_value()) {
    // Store the processed value for next delta calculation
    _previous_sample = NormalizedSampledData{raw_sample.operation_id, *processed_value, raw_sample.timestamp};
    return ASTL_STATUS_SUCCESS;
  }

  // Calculate delta between current and previous sample (both processed)
  auto delta_result = CalculateDelta(*processed_value, _previous_sample->value);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR("DeltaMetric: failed to calculate delta for metric {}: {}", _configuration->Name(),
                   astlStatusString(delta_result.error()));
    return delta_result.error();
  }
  // Forward the delta as a processed sample to the sink
  ProcessedSampledData processed_sampled_data{delta_result.value(), raw_sample.timestamp};
  SinkProcessedSample(processed_sampled_data);

  // Update delta statistics
  auto status = UpdateDeltaStatistics(delta_result.value());
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Store current processed sample as previous for next iteration
  _previous_sample = NormalizedSampledData{raw_sample.operation_id, *processed_value, raw_sample.timestamp};

  return ASTL_STATUS_SUCCESS;
}

/* static */ auto DeltaMetric::CalculateDelta(const AstlValue& current_sample, const AstlValue& previous_sample)
    -> std::expected<AstlValue, astl_status_code> {
  if (!current_sample.IsArithmetic() || !previous_sample.IsArithmetic()) {
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }

  // Calculate delta: current - previous
  auto delta_result = AstlValue::Subtract(current_sample, previous_sample);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR(
        "DeltaMetric: error {} when computing delta between current and previous samples. Current: {}, Previous: {}",
        astlStatusString(delta_result.error()), to_string(current_sample), to_string(previous_sample));
    return std::unexpected(delta_result.error());
  }

  return delta_result.value();
}

auto DeltaMetric::ProcessPauseSample(ProcessedSampleTimestamp pause_timestamp) -> astl_status_code {
  // Drop the previous sample so the first post-resume raw sample starts a fresh delta
  // window rather than computing a delta across the pause gap.
  _previous_sample = std::nullopt;
  ASTL_LOG_INFO("DeltaMetric {}: reset previous sample on pause at {} ns", _configuration->Name(),
                pause_timestamp.time_since_epoch().count());
  // Propagate the pause-marker sentinel downstream.
  return RawMetric::ProcessPauseSample(pause_timestamp);
}

auto DeltaMetric::UpdateDeltaStatistics(const AstlValue& delta_value) -> astl_status_code {
  if (!delta_value.IsArithmetic()) {
    ASTL_LOG_TRACE("DeltaMetric: received delta with non-arithmetic value type for metric: {}", _configuration->Name());
    return ASTL_STATUS_SUCCESS;
  }

  // Update min and max delta values
  _delta_summary_data.min_delta = _delta_summary_data.min_delta.has_value()
                                      ? std::min(_delta_summary_data.min_delta.value(), delta_value)
                                      : delta_value;
  _delta_summary_data.max_delta = _delta_summary_data.max_delta.has_value()
                                      ? std::max(_delta_summary_data.max_delta.value(), delta_value)
                                      : delta_value;

  // Increment delta count
  ++_delta_count;

  // Update sum for average calculation
  auto new_sum_for_avg = AstlValue::Add(delta_value, _sum_delta_value);
  if (!new_sum_for_avg.has_value()) {
    return new_sum_for_avg.error();
  }
  _sum_delta_value = new_sum_for_avg.value();

  // Log the delta value
  _delta_summary_logger.LogInfo("{}, {}, {}, {} \n", _configuration->Name(), _configuration->Description(),
                                _configuration->Units(), delta_value);

  return ASTL_STATUS_SUCCESS;
}

auto DeltaMetric::Summarize() -> astl_status_code {
  // Compute min, max, and average delta values
  if (_delta_count == 0) {
    _delta_summary_logger.LogInfo("No deltas to summarize for metric: {}.", _configuration->Name());
    return ASTL_STATUS_SUCCESS;
  }
  auto average = AstlValue::Divide(_sum_delta_value, _delta_count);
  if (average.has_value()) {
    _delta_summary_data.avg_delta = average.value();
  } else {
    ASTL_LOG_ERROR("Error computing average delta value for metric {}: {}", _configuration->Name(),
                   astlStatusString(average.error()));
  }

  const auto optional_to_string = [](const auto& optional_value) -> std::string {
    return optional_value.has_value() ? to_string(optional_value.value()) : std::string{"<none>"};
  };
  const auto max_delta = optional_to_string(_delta_summary_data.max_delta);
  const auto min_delta = optional_to_string(_delta_summary_data.min_delta);
  const auto avg_delta = optional_to_string(_delta_summary_data.avg_delta);
  _delta_summary_logger.LogInfo(
      "SUMMARY - Metric: {}, Description: {}, Units: {}, Max Delta: {}, Min Delta: {}, Avg Delta: {}, Delta Count: {}, "
      "Type: {} \n",
      _configuration->Name(), _configuration->Description(), _configuration->Units(), max_delta, min_delta, avg_delta,
      _delta_count, _configuration->ValueType());

  return ASTL_STATUS_SUCCESS;
}

auto DeltaMetric::GetDeltaSummaryData() const -> DeltaSummaryData { return _delta_summary_data; }

auto DeltaMetric::Reset() -> void { InitializeSamples(); }

auto DeltaMetric::InitializeSamples() -> void {
  // Reset the metric state
  _previous_sample.reset();
  _delta_summary_data = DeltaSummaryData{};
  _delta_count        = 0;

  // Initialize sum for delta calculations
  auto from_union_result = AstlValue::FromUnionPromoting(_configuration->ValueType());
  if (from_union_result.has_value()) {
    _sum_delta_value = from_union_result.value();
  } else {
    ASTL_LOG_ERROR(
        "DeltaMetric: failed to create initial sum for metric: "
        "{} with type {} because it's a non-arithmetic type",
        _configuration->Name(), _configuration->ValueType());
  }

  // Initialize delta summary data based on the value type
  auto zero_val = AstlValue::FromZero(_configuration->ValueType());
  if (zero_val.has_value()) {
    _delta_summary_data = {.min_delta = std::nullopt, .max_delta = std::nullopt, .avg_delta = zero_val.value()};
  } else {
    ASTL_LOG_INFO("DeltaMetric: unsupported type {} for delta statistics for metric: {}", _configuration->ValueType(),
                  _configuration->Name());
  }
}

}  // namespace astl
