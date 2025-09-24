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

#include "finite_set_metric.hpp"

namespace astl {

FiniteSetMetric::FiniteSetMetric(const char* name, const char* description, astl_units_t units,
                                 astl_value_type_t value_type, const std::set<AstlValue>& finite_set,
                                 const ITarget* target, IProcessedSampleSink* processed_sample_sink)
    : SampledValueMetric(name, description, units, value_type, target, processed_sample_sink),
      _finite_set{finite_set},
      _finite_set_summary{} {}

astl_status_code FiniteSetMetric::ReceiveRawSample(const RawSampledData& raw_sample) {
  // First call the parent class to handle basic sample processing
  auto status = SampledValueMetric::ReceiveRawSample(raw_sample);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Update finite set specific statistics
  return UpdateFiniteSetStatistics(raw_sample);
}

void FiniteSetMetric::Reset() {
  SampledValueMetric::Reset();
  _finite_set_summary = FiniteSetSummaryData{};
}

astl_status_code FiniteSetMetric::Summarize() {
  // Call parent class summarize first
  auto status = SampledValueMetric::Summarize();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Generate finite set specific summary
  LogFiniteSetSummary();
  return ASTL_STATUS_SUCCESS;
}

astl_status_code FiniteSetMetric::UpdateFiniteSetStatistics(const RawSampledData& raw_sample) {
  _finite_set_summary.total_samples++;

  // Check if the value exists in our finite set
  if (IsInFiniteSet(raw_sample.value)) {
    // Value is in the finite set - update counts
    _finite_set_summary.value_counts[raw_sample.value]++;
  } else {
    // Value is not in the finite set - track as unknown
    _finite_set_summary.unknown_values++;
    _finite_set_summary.value_counts[raw_sample.value]++;  // Still track the count

    // Log warning for unknown sample
    std::string warning_msg = "FiniteSetMetric '" + std::string(_name) + "': Received unknown sample value " +
                              to_string(raw_sample.value) + " not in finite set";
    ASTL_LOG_WARNING("{}", warning_msg);
  }

  return ASTL_STATUS_SUCCESS;
}

void FiniteSetMetric::LogFiniteSetSummary() {
  if (_finite_set_summary.total_samples == 0) {
    _finite_summary_logger.LogInfo("No samples to summarize for finite set metric: {}", _name.c_str());
    return;
  }

  // Log header
  _finite_summary_logger.LogInfo("=== Finite Set Metric Summary: {} ===", _name.c_str());
  _finite_summary_logger.LogInfo("Description: {}", _description.c_str());
  _finite_summary_logger.LogInfo("Units: {}", _units);
  _finite_summary_logger.LogInfo("Total Samples: {}", _finite_set_summary.total_samples);
  _finite_summary_logger.LogInfo("Finite Set Size: {}", _finite_set.size());

  if (_finite_set_summary.unknown_values > 0) {
    _finite_summary_logger.LogInfo("Unknown Values: {}", _finite_set_summary.unknown_values);
  }

  _finite_summary_logger.LogInfo("--- Distribution ---");

  // Log known values in the finite set
  for (const auto& value : _finite_set) {
    auto     count_it = _finite_set_summary.value_counts.find(value);
    uint64_t count    = (count_it != _finite_set_summary.value_counts.end()) ? count_it->second : 0;

    std::string log_msg = "Value " + to_string(value) + ": " + std::to_string(count) + " samples";
    _finite_summary_logger.LogInfo("{}", log_msg);
  }

  // Log any unknown values that were encountered
  bool has_unknowns = false;
  for (const auto& pair : _finite_set_summary.value_counts) {
    if (!IsInFiniteSet(pair.first)) {
      if (!has_unknowns) {
        _finite_summary_logger.LogInfo("--- Unknown Values ---");
        has_unknowns = true;
      }

      std::string log_msg = "Unknown Value " + to_string(pair.first) + ": " + std::to_string(pair.second) + " samples";
      _finite_summary_logger.LogInfo("{}", log_msg);
    }
  }
}

}  // namespace astl
