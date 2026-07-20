// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "sampled_value_metric.hpp"

namespace astl {

namespace {
// Initial reservation for processed samples per metric. Chosen as a modest
// size to avoid several small reallocations during the first burst of
// samples while keeping footprint tiny (< 1KB for typical sample structs).
constexpr std::size_t kInitialProcessedSampleCapacity = 16;
}  // namespace

SampledValueMetric::SampledValueMetric(const MetricConfig* configuration, const ITarget* target,
                                       IProcessedSampleSink* processed_sample_sink)
    : RawMetric(configuration, target, processed_sample_sink), _summary_data{}, _sum_sample_value{uint64_t{0}} {
  InitializeSamples();
}

auto SampledValueMetric::ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code {
  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(raw_sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the raw sample using the base class method
  LogNormalizedSample(raw_sample);

  // Apply formula if configured (masking, bit shifting, scaling, etc.)
  auto processed_value = ApplyFormula(raw_sample.value);
  if (!processed_value) {
    ASTL_LOG_ERROR("SampledValueMetric: failed to apply formula for metric: {}, error: {}", _configuration->Name(),
                   astlStatusString(processed_value.error()));
    return processed_value.error();
  }

  ProcessedSampledData processed_sample{*processed_value, raw_sample.timestamp};
  (void)UpdateStatistics(processed_sample);  // statistics errors are logged inside helper
  _processed_samples.push_back(processed_sample);
  // fan-out to manager / external sinks
  SinkProcessedSample(processed_sample);
  return ASTL_STATUS_SUCCESS;
}

auto SampledValueMetric::Reset() -> void {
  std::lock_guard<std::mutex> lock(_samples_mutex);
  InitializeSamples();
}

auto SampledValueMetric::UpdateStatistics(const ProcessedSampledData& processed_sample) -> astl_status_code {
  if (!processed_sample.value.IsArithmetic()) {
    ASTL_LOG_TRACE("SampledValueMetric: received sample with non-arithmetic value type for metric: {}",
                   _configuration->Name());
    return ASTL_STATUS_SUCCESS;
  }
  // For numeric types, update min, max and sum values.
  _summary_data.min = _summary_data.min.has_value() ? std::min(_summary_data.min.value(), processed_sample.value)
                                                    : processed_sample.value;
  _summary_data.max = _summary_data.max.has_value() ? std::max(_summary_data.max.value(), processed_sample.value)
                                                    : processed_sample.value;
  // Update sum for average calculation
  auto new_sum_for_avg = AstlValue::Add(processed_sample.value, _sum_sample_value);
  if (!new_sum_for_avg) {
    return new_sum_for_avg.error();
  }
  _sum_sample_value = *new_sum_for_avg;
  return ASTL_STATUS_SUCCESS;
}

auto SampledValueMetric::InitializeSamples() -> void {
  // Reset the metric state
  if (!_processed_samples.empty()) {
    _processed_samples.clear();
  }
  // Heuristic: pre-reserve a small initial capacity if this is a cold vector. Metrics often receive
  // a handful of samples quickly after configuration; reserving avoids several tiny reallocations.
  // Reserve initial capacity if this is a cold vector.
  if (_processed_samples.capacity() == 0) {
    _processed_samples.reserve(kInitialProcessedSampleCapacity);
  }
  _summary_data          = MinMaxAvgSummaryData{};
  auto from_union_result = AstlValue::FromUnionPromoting(_configuration->ValueType());
  if (from_union_result.has_value()) {
    _sum_sample_value = from_union_result.value();
  } else {
    ASTL_LOG_ERROR(
        "SampledValueMetric: failed to create initial sum for metric: "
        "{} with type {} because it's a non-arithmetic type",
        _configuration->Name(), _configuration->ValueType());
  }
  // Initialize summary data based on the value type
  auto zero_val = AstlValue::FromZero(_configuration->ValueType());
  if (zero_val.has_value()) {
    _summary_data = {.min = std::nullopt, .max = std::nullopt, .avg = zero_val.value()};
  } else {
    ASTL_LOG_INFO("SampledValueMetric: unsupported type {} for statistics for metric: {}", _configuration->ValueType(),
                  _configuration->Name());
  }
}

auto SampledValueMetric::Summarize() -> astl_status_code {
  // Compute min, max, and average values for the received samples.
  // Only one numeric type is valid for a given metric instance.
  std::lock_guard<std::mutex> lock(_samples_mutex);
  if (_processed_samples.empty()) {
    _summary_logger.LogInfo("No samples to summarize.\n");
    return ASTL_STATUS_SUCCESS;
  }
  auto average = AstlValue::Divide(_sum_sample_value, _processed_samples.size());
  if (average) {
    _summary_data.avg = average.value();
  } else {
    ASTL_LOG_ERROR("Error computing average sample value: {}", astlStatusString(average.error()));
  }
  auto to_string_or_none = [](const std::optional<AstlValue>& val) {
    return val.has_value() ? to_string(val.value()) : std::string{"<none>"};
  };
  const auto max_value = to_string_or_none(_summary_data.max);
  const auto min_value = to_string_or_none(_summary_data.min);
  const auto avg_value = to_string_or_none(_summary_data.avg);
  // LOG : Metric, Description, Units, Maximum Value, Minimum Value, Average Value, Type
  _summary_logger.LogInfo("{}, {}, {}, {}, {}, {}, {} \n", _configuration->Name(), _configuration->Description(),
                          _configuration->Units(), max_value, min_value, avg_value, _configuration->ValueType());
  return ASTL_STATUS_SUCCESS;
}

auto SampledValueMetric::GetSummaryData() const -> MinMaxAvgSummaryData { return _summary_data; }

}  // namespace astl
