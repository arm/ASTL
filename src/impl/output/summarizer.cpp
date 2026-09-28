// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "summarizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../common/i_processed_sample_sink.hpp"
#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace astl {

// Internal helper to construct equal-width bins for ranged histograms
namespace {
auto MakeEqualWidthBins(double data_min, double data_max, std::size_t num_bins, HistogramSummary& summary) -> void {
  const double data_range = data_max - data_min;
  const double bin_width  = data_range / static_cast<double>(num_bins);
  summary.bins.clear();
  summary.bins.reserve(num_bins);
  constexpr double k_last_bin_epsilon = 1e-10;  // Make last bin inclusive
  for (std::size_t i = 0; i < num_bins; ++i) {
    const double lower_bound = data_min + (static_cast<double>(i) * bin_width);
    const double upper_bound =
        (i == num_bins - 1) ? data_max + k_last_bin_epsilon : data_min + (static_cast<double>(i + 1) * bin_width);
    summary.bins.emplace_back(lower_bound, upper_bound, 0);
  }
}

// Internal Helper function to check if a value type is arithmetic
constexpr auto IsArithmeticValueType(astl_value_type_t value_type) -> bool {
  return value_type == ASTL_VALUE_UINT8 || value_type == ASTL_VALUE_UINT16 || value_type == ASTL_VALUE_UINT32 ||
         value_type == ASTL_VALUE_UINT64 || value_type == ASTL_VALUE_FLOAT32 || value_type == ASTL_VALUE_FLOAT64;
}

}  // namespace

auto ComputeTimeWeightedAverage(std::span<const ProcessedSampledData>     samples,
                                std::span<const ProcessedSampleTimestamp> pause_markers)
    -> std::expected<std::optional<AstlValue>, astl_status_code> {
  TimeWeightedAvgAccumulator accumulator{pause_markers};
  if (const auto status = accumulator.Add(samples); status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  auto result = accumulator.Result();
  if (!result) {
    return std::unexpected(result.error());
  }
  return result->time_weighted_avg;
}

TimeWeightedAvgAccumulator::TimeWeightedAvgAccumulator(std::span<const ProcessedSampleTimestamp> pause_markers)
    : _pause_markers{pause_markers.begin(), pause_markers.end()} {
  std::sort(_pause_markers.begin(), _pause_markers.end());
}

auto TimeWeightedAvgAccumulator::Add(std::span<const ProcessedSampledData> samples) -> astl_status_code {
  if (samples.size() > std::numeric_limits<std::size_t>::max() - _count) {
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }
  _count += samples.size();
  for (const auto& sample : samples) {
    if (const auto status = AddArithmeticSample(sample); status != ASTL_STATUS_SUCCESS) {
      return status;
    }
    if (const auto status = AddWeightedInterval(sample); status != ASTL_STATUS_SUCCESS) {
      return status;
    }
    _previous_value     = sample.value;
    _previous_timestamp = sample.timestamp;
  }
  return ASTL_STATUS_SUCCESS;
}

auto TimeWeightedAvgAccumulator::AddArithmeticSample(const ProcessedSampledData& sample) -> astl_status_code {
  if (!sample.value.IsArithmetic()) {
    return ASTL_STATUS_SUCCESS;
  }
  auto value = sample.value.ToDouble();
  if (!value) {
    return value.error();
  }
  _arithmetic_sum += *value;
  ++_arithmetic_count;
  return ASTL_STATUS_SUCCESS;
}

auto TimeWeightedAvgAccumulator::AddWeightedInterval(const ProcessedSampledData& sample) -> astl_status_code {
  if (!_previous_value || !_previous_timestamp || !_previous_value->IsArithmetic() || !sample.value.IsArithmetic()) {
    return ASTL_STATUS_SUCCESS;
  }
  auto       interval_end = sample.timestamp;
  const auto pause        = std::upper_bound(_pause_markers.begin(), _pause_markers.end(), *_previous_timestamp);
  if (pause != _pause_markers.end() && *pause < sample.timestamp) {
    interval_end = *pause;
  }
  if (interval_end <= *_previous_timestamp) {
    return ASTL_STATUS_SUCCESS;
  }

  auto value = _previous_value->ToDouble();
  if (!value) {
    return value.error();
  }
  const auto weight =
      static_cast<double>(interval_end.time_since_epoch().count() - _previous_timestamp->time_since_epoch().count());
  _weighted_sum += *value * weight;
  _total_weight += weight;
  return ASTL_STATUS_SUCCESS;
}

auto TimeWeightedAvgAccumulator::Result() const -> std::expected<TimeWeightedAvgSummary, astl_status_code> {
  TimeWeightedAvgSummary summary{};
  summary.count = _count;
  if (_total_weight > 0.0) {
    summary.time_weighted_avg = AstlValue{std::round((_weighted_sum / _total_weight) * 100.0) / 100.0};
  } else if (_arithmetic_count > 0) {
    summary.time_weighted_avg =
        AstlValue{std::round((_arithmetic_sum / static_cast<double>(_arithmetic_count)) * 100.0) / 100.0};
  }
  return summary;
}

// MinMaxAvgSummarizer Implementation
auto MinMaxAvgAccumulator::Add(std::span<const ProcessedSampledData> samples) -> astl_status_code {
  if (samples.size() > std::numeric_limits<std::size_t>::max() - _summary.count) {
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }
  _summary.count += samples.size();
  for (const auto& sample : samples) {
    if (!sample.value.IsArithmetic()) {
      continue;
    }
    _summary.min = std::min(sample.value, _summary.min.value_or(sample.value));
    _summary.max = std::max(sample.value, _summary.max.value_or(sample.value));
    if (!_sum) {
      const auto [unused, type] = sample.value.ToAstlUnionValue();
      static_cast<void>(unused);
      auto zero = AstlValue::FromUnionPromoting(type);
      if (!zero) {
        return ASTL_STATUS_INTERNAL_ERROR;
      }
      _sum = *zero;
    }
    auto added = AstlValue::Add(*_sum, sample.value);
    if (!added) {
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    _sum = *added;
    ++_arithmetic_count;
  }
  return ASTL_STATUS_SUCCESS;
}

auto MinMaxAvgAccumulator::Result() const -> std::expected<MinMaxAvgSummary, astl_status_code> {
  auto result = _summary;
  if (_arithmetic_count == 0) {
    return result;
  }
  auto average = AstlValue::Divide(*_sum, static_cast<double>(_arithmetic_count));
  if (!average) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  const double rounded = std::round(std::get<double>(average->value) * 100.0) / 100.0;
  result.avg           = AstlValue{rounded};
  return result;
}

auto MinMaxAvgSummarizer::Summarize(std::span<const ProcessedSampledData> samples) const
    -> std::expected<SummaryResult, astl_status_code> {
  MinMaxAvgAccumulator accumulator;
  if (const auto status = accumulator.Add(samples); status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  auto result = accumulator.Result();
  if (!result) {
    return std::unexpected(result.error());
  }
  return *result;
}

auto MinMaxAvgSummarizer::IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const -> bool {
  return (metric_type == ASTL_METRIC_VALUE || metric_type == ASTL_METRIC_DELTA || metric_type == ASTL_METRIC_RATE) &&
         IsArithmeticValueType(value_type);
}

// TimeWeightedAvgSummarizer Implementation
auto TimeWeightedAvgSummarizer::Summarize(std::span<const ProcessedSampledData> samples) const
    -> std::expected<SummaryResult, astl_status_code> {
  return Summarize(samples, {});
}

auto TimeWeightedAvgSummarizer::Summarize(std::span<const ProcessedSampledData>     samples,
                                          std::span<const ProcessedSampleTimestamp> pause_markers)
    -> std::expected<SummaryResult, astl_status_code> {
  if (samples.empty()) {
    ASTL_LOG_TRACE("TimeWeightedAvgSummarizer: No samples to summarize");
    return TimeWeightedAvgSummary{std::nullopt, 0};
  }

  TimeWeightedAvgSummary summary{};
  summary.count = samples.size();

  auto result = ComputeTimeWeightedAverage(samples, pause_markers);
  if (!result.has_value()) {
    ASTL_LOG_ERROR("TimeWeightedAvgSummarizer: Failed to compute time-weighted average");
    return std::unexpected(result.error());
  }

  summary.time_weighted_avg = result.value();
  return summary;
}

auto TimeWeightedAvgSummarizer::IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const
    -> bool {
  return (metric_type == ASTL_METRIC_VALUE || metric_type == ASTL_METRIC_DELTA || metric_type == ASTL_METRIC_RATE) &&
         IsArithmeticValueType(value_type);
}

// HistogramSummarizer Implementation
auto HistogramSummarizer::Summarize(std::span<const ProcessedSampledData> samples) const
    -> std::expected<SummaryResult, astl_status_code> {
  if (samples.empty()) {
    ASTL_LOG_TRACE("HistogramSummarizer: No samples to summarize");
    HistogramSummary empty_summary{};
    empty_summary.total_count = 0;
    empty_summary.is_discrete = use_discrete_bins_;
    return empty_summary;
  }

  return use_discrete_bins_ ? SummarizeDiscrete(samples) : SummarizeRanged(samples);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto HistogramSummarizer::SummarizeDiscrete(std::span<const ProcessedSampledData> samples) const
    -> std::expected<SummaryResult, astl_status_code> {
  DiscreteHistogramAccumulator accumulator;
  if (const auto status = accumulator.Add(samples); status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  return accumulator.Result();
}

auto DiscreteHistogramAccumulator::Add(std::span<const ProcessedSampledData> samples) -> astl_status_code {
  if (samples.size() > std::numeric_limits<std::size_t>::max() - _total_count) {
    return ASTL_STATUS_BUFFER_TOO_SMALL;
  }
  _total_count += samples.size();
  constexpr std::size_t k_max_discrete_bins = 1000;
  for (const auto& sample : samples) {
    if (_too_many_bins) {
      continue;
    }
    ++_histogram[sample.value];
    if (_histogram.size() > k_max_discrete_bins) {
      ASTL_LOG_WARNING(
          "DiscreteHistogramAccumulator: Too many unique values ({} > {}) in discrete mode; histogram bins will be "
          "omitted.",
          _histogram.size(), k_max_discrete_bins);
      _too_many_bins = true;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto DiscreteHistogramAccumulator::Result() const -> HistogramSummary {
  HistogramSummary summary{};
  summary.total_count   = _total_count;
  summary.is_discrete   = true;
  summary.unique_values = _histogram.size();
  if (_too_many_bins) {
    return summary;
  }
  summary.bins.reserve(_histogram.size());
  for (const auto& [value, count] : _histogram) {
    summary.bins.emplace_back(value, count);
  }
  return summary;
}

auto HistogramSummarizer::SummarizeRanged(std::span<const ProcessedSampledData> samples) const
    -> std::expected<SummaryResult, astl_status_code> {
  HistogramSummary summary{};
  summary.total_count = samples.size();
  summary.is_discrete = false;

  // Convert all arithmetic samples to double for histogram processing.
  // This unifies different numeric types (uint8, uint16, uint32, uint64, float32, float64)
  // into a single type for bin boundary calculation and range-based bin placement comparisons.
  std::vector<double> arithmetic_values;
  arithmetic_values.reserve(samples.size());

  for (const auto& sample : samples) {
    if (!sample.value.IsArithmetic()) {
      ASTL_LOG_ERROR("HistogramSummarizer: Non-arithmetic value encountered in range mode");
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    // Convert to double for histogram processing
    auto double_result = sample.value.ToDouble();
    if (!double_result) {
      ASTL_LOG_ERROR("HistogramSummarizer: Non-arithmetic value encountered in range mode");
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    arithmetic_values.push_back(double_result.value());
  }

  if (arithmetic_values.empty()) {
    ASTL_LOG_TRACE("HistogramSummarizer: No arithmetic samples found");
    return summary;
  }

  /**
   * @brief Implements range-based histogram binning for arithmetic (numeric) values.
   *
   * The algorithm proceeds as follows:
   *   1. Find the minimum and maximum values in the sample set (to determine the data range).
   *   2. If all values are identical, create a single bin padded on both sides for clarity.
   *   3. Otherwise, divide the range into N equal-width bins, where N = num_bins_.
   *      Each bin is defined by a lower and upper bound. The last bin is made slightly wider
   *      (using a small epsilon) to ensure the maximum value is included.
   *   4. For each value, determine which bin it belongs to and increment that bin's count.
   *      The last bin is inclusive of its upper bound; all others are exclusive.
   *   5. If a value does not fit any bin (should not happen), count it as out-of-range and log an error.
   *
   */

  // Find the minimum and maximum values in the sample set
  auto min_it = std::min_element(arithmetic_values.begin(), arithmetic_values.end());
  auto max_it = std::max_element(arithmetic_values.begin(), arithmetic_values.end());

  double data_min   = *min_it;
  double data_max   = *max_it;
  double data_range = data_max - data_min;

  // Handle edge case where all values are the same
  constexpr double k_single_value_bin_padding = 0.5;  // Padding for single-value histogram bins
  if (data_range == 0.0) {
    summary.bins.emplace_back(data_min - k_single_value_bin_padding, data_max + k_single_value_bin_padding,
                              arithmetic_values.size());
    return summary;
  }

  // Create bins with equal width
  MakeEqualWidthBins(data_min, data_max, num_bins_, summary);

  // Populate bins with sample counts
  for (double value : arithmetic_values) {
    bool placed = false;
    for (auto& bin : summary.bins) {
      // For the last bin, include the upper bound; for others, exclude it
      bool in_bin = (&bin == &summary.bins.back()) ? (value >= bin.lower_bound && value <= bin.upper_bound)
                                                   : (value >= bin.lower_bound && value < bin.upper_bound);

      if (in_bin) {
        bin.count++;
        placed = true;
        break;
      }
    }

    if (!placed) {
      summary.out_of_range_count++;
      ASTL_LOG_ERROR("HistogramSummarizer: Value %.2f fell outside all bin ranges [%.2f, %.2f]", value, data_min,
                     data_max);
    }
  }

  if (summary.out_of_range_count > 0) {
    ASTL_LOG_ERROR("HistogramSummarizer: %zu values fell outside bin ranges", summary.out_of_range_count);
  }

  return summary;
}

auto HistogramSummarizer::IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const -> bool {
  // Only support VALUE, FINITE_SET_VALUE, and EVENT metric types
  if (metric_type != ASTL_METRIC_VALUE && metric_type != ASTL_METRIC_FINITE_SET_VALUE &&
      metric_type != ASTL_METRIC_EVENT) {
    return false;
  }

  // For range mode, require arithmetic value types
  if (!use_discrete_bins_) {
    return IsArithmeticValueType(value_type);
  }

  // Discrete mode: support all value types
  return true;
}

}  // namespace astl
