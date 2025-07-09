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

#include "sampled_delta_metric.hpp"

#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace astl {

DeltaMetric::DeltaMetric(const char* name, const char* description, astl_units_t units, astl_value_type_t value_type)
    : RawMetric(name, description, units, value_type, ASTL_METRIC_DELTA),
      _previous_sample{std::nullopt},
      _delta_summary_data{},
      _sum_delta_value{uint64_t{0}} {
  InitializeSamples();

  // Header initialization for delta summary logger
  _delta_summary_logger.LogInfo("Metric, Description, Units, Delta Value \n");
}

astl_status_code DeltaMetric::ReceiveSample(const SampledData& sample) {
  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the raw sample using the base class method
  LogRawSample(sample);

  // If this is the first sample, store it and return
  if (!_previous_sample.has_value()) {
    _previous_sample = sample;
    return ASTL_STATUS_SUCCESS;
  }

  // Calculate delta between current and previous sample
  auto delta_result = CalculateDelta(sample.value, _previous_sample->value);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR("DeltaMetric: failed to calculate delta for metric {}: {}", _name.c_str(),
                   astlStatusString(delta_result.error()));
    return delta_result.error();
  }

  // Update delta statistics
  auto status = UpdateDeltaStatistics(delta_result.value(), sample.timestamp);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Store current sample as previous for next iteration
  _previous_sample = sample;

  return ASTL_STATUS_SUCCESS;
}

/* static */ std::expected<AstlValue, astl_status_code> DeltaMetric::CalculateDelta(const AstlValue& current_sample,
                                                                                    const AstlValue& previous_sample) {
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

astl_status_code DeltaMetric::UpdateDeltaStatistics(const AstlValue& delta_value, SampleTimestamp timestamp) {
  if (!delta_value.IsArithmetic()) {
    ASTL_LOG_TRACE("DeltaMetric: received delta with non-arithmetic value type for metric: {}", _name);
    return ASTL_STATUS_SUCCESS;
  }

  // Update min and max delta values
  _delta_summary_data.min_delta = _delta_summary_data.min_delta.has_value()
                                      ? std::min(_delta_summary_data.min_delta.value(), delta_value)
                                      : delta_value;
  _delta_summary_data.max_delta = _delta_summary_data.max_delta.has_value()
                                      ? std::max(_delta_summary_data.max_delta.value(), delta_value)
                                      : delta_value;

  // Store delta data for later analysis and GetSamples invocation.
  _deltas.push_back({delta_value, timestamp});

  // Update sum for average calculation
  auto new_sum_for_avg = AstlValue::Add(delta_value, _sum_delta_value);
  if (!new_sum_for_avg.has_value()) {
    return new_sum_for_avg.error();
  }
  _sum_delta_value = new_sum_for_avg.value();

  // Log the delta value
  _delta_summary_logger.LogInfo("{}, {}, {}, {} \n", _name.c_str(), _description.c_str(), _units, delta_value);

  return ASTL_STATUS_SUCCESS;
}

astl_status_code DeltaMetric::Summarize() {
  // Compute min, max, and average delta values
  if (_deltas.empty()) {
    _delta_summary_logger.LogInfo("No deltas to summarize for metric: {}.", _name.c_str());
    return ASTL_STATUS_SUCCESS;
  }

  if (!_deltas.empty()) {
    auto average = AstlValue::Divide(_sum_delta_value, _deltas.size());
    if (average.has_value()) {
      _delta_summary_data.avg_delta = average.value();
    } else {
      ASTL_LOG_ERROR("Error computing average delta value for metric {}: {}", _name.c_str(),
                     astlStatusString(average.error()));
    }
  }

  auto none = AstlValue{std::string{"<none>"}};
  _delta_summary_logger.LogInfo(
      "SUMMARY - Metric: {}, Description: {}, Units: {}, Max Delta: {}, Min Delta: {}, Avg Delta: {}, Delta Count: {}, "
      "Type: {} \n",
      _name.c_str(), _description.c_str(), _units, _delta_summary_data.max_delta.value_or(none),
      _delta_summary_data.min_delta.value_or(none), _delta_summary_data.avg_delta.value_or(none), _deltas.size(),
      _value_type);

  return ASTL_STATUS_SUCCESS;
}

DeltaSummaryData DeltaMetric::GetDeltaSummaryData() const { return _delta_summary_data; }

std::span<const SampledData> DeltaMetric::GetSamples() const {
  // DeltaMetric doesn't store original samples, only delta data
  // Return an empty span since we don't have access to the original samples
  // TODO(ASTL-58): when OutputManager is implemented, revisit to see if DeltaData can be send.
  static const std::vector<SampledData> empty_samples;
  return std::span<const SampledData>(empty_samples);
}

std::span<const DeltaData> DeltaMetric::GetDeltas() const { return std::span<const DeltaData>(_deltas); }

void DeltaMetric::Reset() { InitializeSamples(); }

void DeltaMetric::InitializeSamples() {
  // Reset the metric state
  _previous_sample.reset();
  _delta_summary_data = DeltaSummaryData{};
  _deltas.clear();

  // Initialize sum for delta calculations
  auto from_union_result = AstlValue::FromUnionPromoting(_value_type);
  if (from_union_result.has_value()) {
    _sum_delta_value = from_union_result.value();
  } else {
    ASTL_LOG_ERROR(
        "DeltaMetric: failed to create initial sum for metric: "
        "{} with type {} because it's a non-arithmetic type",
        _name, _value_type);
  }

  // Initialize delta summary data based on the value type
  auto zero_val = AstlValue::FromZero(_value_type);
  if (zero_val.has_value()) {
    _delta_summary_data = {.min_delta = std::nullopt, .max_delta = std::nullopt, .avg_delta = zero_val.value()};
  } else {
    ASTL_LOG_INFO("DeltaMetric: unsupported type {} for delta statistics for metric: {}", _value_type, _name);
  }
}

}  // namespace astl
