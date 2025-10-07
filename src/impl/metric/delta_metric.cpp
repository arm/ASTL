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

auto DeltaMetric::ReceiveRawSample(const RawSampledData& raw_sample) -> astl_status_code {
  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(raw_sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the raw sample using the base class method
  LogRawSample(raw_sample);

  // If this is the first sample, store it and return
  if (!_previous_sample.has_value()) {
    _previous_sample = raw_sample;
    return ASTL_STATUS_SUCCESS;
  }

  // Calculate delta between current and previous sample
  auto delta_result = CalculateDelta(raw_sample.value, _previous_sample->value);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR("DeltaMetric: failed to calculate delta for metric {}: {}", _configuration->Name(),
                   astlStatusString(delta_result.error()));
    return delta_result.error();
  }

  // Update delta statistics
  auto status = UpdateDeltaStatistics(delta_result.value(), raw_sample.timestamp);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Store current raw sample as previous for next iteration
  _previous_sample = raw_sample;

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
    return std::unexpected(delta_result.error());
  }

  return delta_result.value();
}

auto DeltaMetric::UpdateDeltaStatistics(const AstlValue& delta_value, SampleTimestamp timestamp) -> astl_status_code {
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

  ProcessedSampledData processed_sampled_data{delta_value, timestamp};

  SinkProcessedSample(processed_sampled_data);

  // Store delta data for later analysis and GetProcessedSamples invocation.
  _deltas.push_back({delta_value, timestamp});

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
  if (_deltas.empty()) {
    _delta_summary_logger.LogInfo("No deltas to summarize for metric: {}.", _configuration->Name());
    return ASTL_STATUS_SUCCESS;
  }
  auto average = AstlValue::Divide(_sum_delta_value, _deltas.size());
  if (average.has_value()) {
    _delta_summary_data.avg_delta = average.value();
  } else {
    ASTL_LOG_ERROR("Error computing average delta value for metric {}: {}", _configuration->Name(),
                   astlStatusString(average.error()));
  }

  auto none = AstlValue{std::string{"<none>"}};
  _delta_summary_logger.LogInfo(
      "SUMMARY - Metric: {}, Description: {}, Units: {}, Max Delta: {}, Min Delta: {}, Avg Delta: {}, Delta Count: {}, "
      "Type: {} \n",
      _configuration->Name(), _configuration->Description(), _configuration->Units(),
      _delta_summary_data.max_delta.value_or(none), _delta_summary_data.min_delta.value_or(none),
      _delta_summary_data.avg_delta.value_or(none), _deltas.size(), _configuration->ValueType());

  return ASTL_STATUS_SUCCESS;
}

auto DeltaMetric::GetDeltaSummaryData() const -> DeltaSummaryData { return _delta_summary_data; }

auto DeltaMetric::GetProcessedSamples() const -> std::span<const ProcessedSampledData> {
  return std::span<const ProcessedSampledData>(_deltas);
}

auto DeltaMetric::Reset() -> void { InitializeSamples(); }

auto DeltaMetric::InitializeSamples() -> void {
  // Reset the metric state
  _previous_sample.reset();
  _delta_summary_data = DeltaSummaryData{};
  _deltas.clear();

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
