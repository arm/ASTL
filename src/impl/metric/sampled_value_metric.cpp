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

#include "sampled_value_metric.hpp"

namespace astl {

SampledValueMetric::SampledValueMetric(const char* name, const char* description, astl_units_t units,
                                       astl_value_type_t value_type)
    : RawMetric(name, description, units, value_type, ASTL_METRIC_VALUE),
      _summary_data{},
      _sum_sample_value{uint64_t{0}} {
  InitializeSamples();
}

astl_status_code SampledValueMetric::ReceiveSample(const SampledData& sample) {
  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the raw sample using the base class method
  LogRawSample(sample);
  auto status = UpdateStatistics(sample);
  _samples.push_back(sample);
  return status;
}

std::span<const SampledData> SampledValueMetric::GetSamples() const {
  // Return a span of the samples received by this metric
  return std::span<const SampledData>(_samples);
}

void SampledValueMetric::Reset() { InitializeSamples(); }

astl_status_code SampledValueMetric::UpdateStatistics(const SampledData& sample) {
  if (!sample.value.IsArithmetic()) {
    ASTL_LOG_TRACE("SampledValueMetric: received sample with non-arithmetic value type for metric: {}", _name);
    return ASTL_STATUS_SUCCESS;
  }
  // For numeric types, update min, max and sum values.
  _summary_data.min = _summary_data.min.has_value() ? std::min(_summary_data.min.value(), sample.value) : sample.value;
  _summary_data.max = _summary_data.max.has_value() ? std::max(_summary_data.max.value(), sample.value) : sample.value;
  // Update sum for average calculation
  auto new_sum_for_avg = AstlValue::Add(sample.value, _sum_sample_value);
  if (!new_sum_for_avg) {
    return new_sum_for_avg.error();
  }
  _sum_sample_value = *new_sum_for_avg;
  return ASTL_STATUS_SUCCESS;
}

void SampledValueMetric::InitializeSamples() {
  // Reset the metric state
  _samples.clear();
  _summary_data          = MinMaxAvgSummaryData{};
  auto from_union_result = AstlValue::FromUnionPromoting(_value_type);
  if (from_union_result.has_value()) {
    _sum_sample_value = from_union_result.value();
  } else {
    ASTL_LOG_ERROR(
        "SampledValueMetric: failed to create initial sum for metric: "
        "{} with type {} because it's a non-arithmetic type",
        _name, _value_type);
  }
  // Initialize summary data based on the value type
  auto zero_val = AstlValue::FromZero(_value_type);
  if (zero_val.has_value()) {
    _summary_data = {.min = std::nullopt, .max = std::nullopt, .avg = zero_val.value()};
  } else {
    ASTL_LOG_INFO("SampledValueMetric: unsupported type {} for statistics for metric: {}", _value_type, _name);
  }
}

astl_status_code SampledValueMetric::Summarize() {
  // Compute min, max, and average values for the received samples.
  // Only one numeric type is valid for a given metric instance.
  if (_samples.empty()) {
    _summary_logger.LogInfo("No samples to summarize.");
    return ASTL_STATUS_SUCCESS;
  }
  auto average = AstlValue::Divide(_sum_sample_value, _samples.size());
  if (average) {
    _summary_data.avg = average.value();
  } else {
    ASTL_LOG_ERROR("Error computing average sample value: {}", astlStatusString(average.error()));
  }
  auto none = AstlValue{std::string{"<none>"}};
  // LOG : Metric, Description, Units, Maximum Value, Minimum Value, Average Value, Type
  _summary_logger.LogInfo("{}, {}, {}, {}, {}, {}, {} \n", _name.c_str(), _description.c_str(), _units,
                          _summary_data.max.value_or(none), _summary_data.min.value_or(none),
                          _summary_data.avg.value_or(none), _value_type);
  return ASTL_STATUS_SUCCESS;
}

MinMaxAvgSummaryData SampledValueMetric::GetSummaryData() const { return _summary_data; }

}  // namespace astl
