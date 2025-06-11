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
    : _name(name),
      _description(description),
      _units(units),
      _value_type(value_type),
      _summary_data{},
      _sum_sample_value{0},
      _sample_count{0} {
  // Initialize summary data based on the value type
  switch (_value_type) {
    case astl_value_type_t::ASTL_VALUE_UINT64:
      _summary_data.min.ui64 = std::numeric_limits<uint64_t>::max();     // Initialize min
      _summary_data.max.ui64 = std::numeric_limits<uint64_t>::lowest();  // Initialize max
      _summary_data.avg.ui64 = 0;                                        // Initialize avg to zero
      break;
    default:
      // Handle other types if necessary or default
      ASTL_LOG_ERROR("SampledValueMetric: unsupported type for metric: {}", _name);
      break;
  }
}
astl_status_code SampledValueMetric::ReceiveSample(const SampledData& sample) {
  // For numeric types, update min, max and sum values.
  switch (_value_type) {
    case astl_value_type_t::ASTL_VALUE_UINT64: {
      uint64_t sample_val    = sample.value.ui64;
      _summary_data.min.ui64 = std::min(_summary_data.min.ui64, sample_val);
      _summary_data.max.ui64 = std::max(_summary_data.max.ui64, sample_val);
      if (UINT64_MAX - _sum_sample_value < sample_val) [[unlikely]] {
        // TODO (https://jira.arm.com/browse/ASTL-100): Handle overflow more gracefully.
        ASTL_LOG_ERROR("SampledValueMetric: Sum overflow detected for metric: {}", _name);
        return ASTL_STATUS_METRIC_OVERFLOW_DETECTED;
      } else {
        _sum_sample_value += sample_val;  // Update sum for average calculation
      }
      _raw_sample_logger.LogInfo("Metric: {}, Description: {}, Units: {%d}, Raw Value: {%llu}, Type: UINT64",
                                 _name.c_str(), _description.c_str(), static_cast<int>(_units), sample_val);
      break;
    }
    default: {
      // Handle other types if necessary or log an error
      ASTL_LOG_ERROR("SampledValueMetric: unsupported type for metric: {}", _name);
      return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
    }
  }
  _sample_count++;
  return ASTL_STATUS_SUCCESS;
}

astl_status_code SampledValueMetric::Summarize() {
  // Compute min, max, and average values for the received samples.
  // Only one numeric type is valid for a given metric instance.
  if (_sample_count == 0) {
    _summary_logger.LogInfo("No samples to summarize.");
    return ASTL_STATUS_SUCCESS;
  }
  // Log using the correct numeric type
  switch (_value_type) {
    case astl_value_type_t::ASTL_VALUE_UINT64:
      // Compute average
      _summary_data.avg.ui64 = _sum_sample_value / _sample_count;  // Update summary data with computed average
      _summary_logger.LogInfo(
          "Metric: {}, Description: {}, Units: {%d}, Maximum Value: {%llu}, Minimum Value: {%llu}, Average Value: "
          "{%llu}, Type: UINT64",
          _name.c_str(), _description.c_str(), static_cast<int>(_units), _summary_data.max.ui64, _summary_data.min.ui64,
          _summary_data.avg.ui64);
      break;

    default:
      ASTL_LOG_ERROR("SampledValueMetric: unsupported type for metric: {}", _name);
      return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }
  return ASTL_STATUS_SUCCESS;
}

MinMaxAvgSummaryData SampledValueMetric::GetSummaryData() const { return _summary_data; }

astl_status_code SampledValueMetric::GetProperties(astl_metric_properties_t* properties) const {
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

}  // namespace astl
