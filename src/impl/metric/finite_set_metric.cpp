// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "finite_set_metric.hpp"

#include "astl_logger.hpp"

namespace astl {

FiniteSetMetric::FiniteSetMetric(const FiniteSetMetricConfig* configuration, const ITarget* target,
                                 IProcessedSampleSink* processed_sample_sink)
    : SampledValueMetric(configuration, target, processed_sample_sink),
      _finite_set_configuration{configuration},
      _finite_set_summary{} {}

auto FiniteSetMetric::ReceiveRawSample(const RawSampledData& raw_sample) -> astl_status_code {
  // First call the parent class to handle basic sample processing
  auto status = SampledValueMetric::ReceiveRawSample(raw_sample);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Update finite set specific statistics
  return UpdateFiniteSetStatistics(raw_sample);
}

auto FiniteSetMetric::Reset() -> void {
  SampledValueMetric::Reset();
  _finite_set_summary = FiniteSetSummaryData{};
}

auto FiniteSetMetric::Summarize() -> astl_status_code {
  // Call parent class summarize first
  auto status = SampledValueMetric::Summarize();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Generate finite set specific summary
  LogFiniteSetSummary();
  return ASTL_STATUS_SUCCESS;
}

auto FiniteSetMetric::UpdateFiniteSetStatistics(const RawSampledData& raw_sample) -> astl_status_code {
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
    std::string warning_msg = "FiniteSetMetric '" + std::string(_configuration->Name()) +
                              "': Received unknown sample value " + to_string(raw_sample.value) + " not in finite set";
    ASTL_LOG_WARNING("{}", warning_msg);
  }

  return ASTL_STATUS_SUCCESS;
}

auto FiniteSetMetric::LogFiniteSetSummary() -> void {
  if (_finite_set_summary.total_samples == 0) {
    _finite_summary_logger.LogInfo("No samples to summarize for finite set metric: {}", _configuration->Name());
    return;
  }

  // Log header
  _finite_summary_logger.LogInfo("=== Finite Set Metric Summary: {} ===", _configuration->Name());
  _finite_summary_logger.LogInfo("\n Description: {}", _configuration->Description());
  _finite_summary_logger.LogInfo("\n Units: {}", _configuration->Units());
  _finite_summary_logger.LogInfo("\n Total Samples: {}", _finite_set_summary.total_samples);
  _finite_summary_logger.LogInfo("\n Finite Set Size: {}", _finite_set_configuration->GetFiniteSet().size());

  if (_finite_set_summary.unknown_values > 0) {
    _finite_summary_logger.LogInfo("\n Unknown Values: {}", _finite_set_summary.unknown_values);
  }

  _finite_summary_logger.LogInfo("\n --- Distribution --- \n");

  // Log known values in the finite set
  for (const auto& value : _finite_set_configuration->GetFiniteSet()) {
    auto     count_it = _finite_set_summary.value_counts.find(value);
    uint64_t count    = (count_it != _finite_set_summary.value_counts.end()) ? count_it->second : 0;

    std::string value_display     = to_string(value);
    auto        state_info_result = _finite_set_configuration->GetStateInfoForValue(value);
    if (state_info_result.has_value()) {
      value_display += " (" + state_info_result.value()->state_name + ")";
    } else {
      ASTL_LOG_ERROR("FiniteSetMetric: no StateInfo for value {} in finite set - this is a configuration error",
                     to_string(value));
    }

    std::string log_msg = "\n Value " + value_display + ": " + std::to_string(count) + " samples";
    _finite_summary_logger.LogInfo("{}", log_msg);
  }

  // Log any unknown values that were encountered
  bool has_unknowns = false;
  for (const auto& pair : _finite_set_summary.value_counts) {
    if (!IsInFiniteSet(pair.first)) {
      if (!has_unknowns) {
        _finite_summary_logger.LogInfo("\n --- Unknown Values ---\n");
        has_unknowns = true;
      }

      std::string log_msg =
          "\n Unknown Value " + to_string(pair.first) + ": " + std::to_string(pair.second) + " samples \n";
      _finite_summary_logger.LogInfo("{}", log_msg);
    }
  }
}

}  // namespace astl
