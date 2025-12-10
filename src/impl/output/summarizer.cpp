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

#include "summarizer.hpp"

#include <algorithm>
#include <cmath>

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

// MinMaxAvgSummarizer Implementation
auto MinMaxAvgSummarizer::Summarize(std::span<const ProcessedSampledData> samples) const
    -> std::expected<SummaryResult, astl_status_code> {
  if (samples.empty()) {
    ASTL_LOG_TRACE("MinMaxAvgSummarizer: No samples to summarize");
    return MinMaxAvgSummary{std::nullopt, std::nullopt, std::nullopt, 0};
  }

  MinMaxAvgSummary summary{};
  summary.count = samples.size();

  // Find first arithmetic sample to initialize min/max
  auto arithmetic_sample_it = std::find_if(
      samples.begin(), samples.end(), [](const ProcessedSampledData& sample) { return sample.value.IsArithmetic(); });

  if (arithmetic_sample_it == samples.end()) {
    ASTL_LOG_TRACE("MinMaxAvgSummarizer: No arithmetic samples found");
    return summary;
  }

  // Initialize min/max with first arithmetic value
  summary.min = arithmetic_sample_it->value;
  summary.max = arithmetic_sample_it->value;

  // Get type for creating a zero value for sum
  auto [union_val, value_type] = arithmetic_sample_it->value.ToAstlUnionValue();
  auto sum_result              = AstlValue::FromUnionPromoting(value_type);
  if (!sum_result) {
    ASTL_LOG_ERROR("MinMaxAvgSummarizer: Failed to create initial sum");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto        sum              = sum_result.value();
  std::size_t arithmetic_count = 0;

  for (const auto& sample : samples) {
    if (!sample.value.IsArithmetic()) {
      continue;
    }

    // Update min/max
    summary.min = std::min(sample.value, summary.min.value());
    summary.max = std::max(sample.value, summary.max.value());

    // Add to sum
    auto add_result = AstlValue::Add(sum, sample.value);
    if (!add_result) {
      ASTL_LOG_ERROR("MinMaxAvgSummarizer: Failed to add sample to sum");
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    sum = add_result.value();
    arithmetic_count++;
  }

  if (arithmetic_count == 0) {
    ASTL_LOG_DEBUG("MinMaxAvgSummarizer: No arithmetic samples to calculate average");
    return summary;
  }
  // Calculate average
  auto avg_result = AstlValue::Divide(sum, static_cast<double>(arithmetic_count));
  if (avg_result) {
    // Extract the double value and round to 2 decimal places
    double avg_value   = std::get<double>(avg_result.value().value);
    double rounded_avg = std::round(avg_value * 100.0) / 100.0;
    summary.avg        = AstlValue{rounded_avg};
  } else {
    ASTL_LOG_ERROR("MinMaxAvgSummarizer: Failed to calculate average");
  }

  return summary;
}

auto MinMaxAvgSummarizer::IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const -> bool {
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
  HistogramSummary summary{};
  summary.total_count = samples.size();
  summary.is_discrete = true;

  // Use a map to count occurrences of each unique value (map keeps values sorted)
  std::map<AstlValue, std::size_t> histogram;
  // Check for excessive unique values (potentially pathological input)
  constexpr std::size_t k_max_discrete_bins = 1000;

  /**
   * @brief SummarizeDiscrete provides sorted Histogram that is stored as a vector.
   *
   * @details `std::map` keeps keys (values) in sorted order while we tally
   * counts. We then transform that ordered data into a vector of summary.bins
   * The downstream writers can iterate sequentially with cache-friendly
   * access and emit output that is already sorted by value.
   *
   */
  summary.bins.reserve(histogram.size());
  for (const auto& sample : samples) {
    // Count occurrences of each value (discrete mode supports all types)
    histogram[sample.value]++;
    if (histogram.size() > k_max_discrete_bins) {
      ASTL_LOG_WARNING("HistogramSummarizer: Too many unique values (%zu > %zu) in discrete mode; summary omitted.",
                       histogram.size(), k_max_discrete_bins);
      // Do not populate summary.bins, return summary
      summary.unique_values = histogram.size();
      return summary;
    }
  }

  if (histogram.empty()) {
    ASTL_LOG_TRACE("HistogramSummarizer: No samples found");
    return summary;
  }

  // Create bins for each unique value (already sorted by std::map)
  summary.bins.reserve(histogram.size());
  for (const auto& [value, count] : histogram) {
    summary.bins.emplace_back(value, count);
  }

  summary.unique_values = histogram.size();

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
