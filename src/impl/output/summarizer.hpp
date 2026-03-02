// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_SUMMARIZER_HPP_
#define ASTL_SUMMARIZER_HPP_

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "astl/astl.h"
#include "common/astl_value.hpp"

namespace astl {

// Forward declarations
struct ProcessedSampledData;

/**
 * @brief Generic summary result that can hold different types of summary data.
 */
using SummaryResult = std::variant<struct MinMaxAvgSummary, struct HistogramSummary>;

/**
 * @brief Summary data for min/max/average statistics.
 */
struct MinMaxAvgSummary {
  std::optional<AstlValue> min;       ///< Minimum value
  std::optional<AstlValue> max;       ///< Maximum value
  std::optional<AstlValue> avg;       ///< Average value
  std::size_t              count{0};  ///< Number of samples processed
};

/**
 * @brief Histogram bin data structure.
 */
struct HistogramBin {
  AstlValue   value;        ///< The exact value for this bin (for discrete binning)
  double      lower_bound;  ///< Lower bound of the bin (inclusive, for range binning)
  double      upper_bound;  ///< Upper bound of the bin (exclusive for all bins except the last, for range binning)
  std::size_t count;        ///< Number of samples in this bin
  bool        is_discrete;  ///< True if this is a discrete value bin, false if it's a range bin

  // Constructor for discrete value bins
  explicit HistogramBin(AstlValue value_arg, std::size_t count_arg = 0)
      : value(value_arg), lower_bound(0.0), upper_bound(0.0), count(count_arg), is_discrete(true) {}

  // Constructor for range bins
  HistogramBin(double lower, double upper, std::size_t cnt = 0)
      : value(AstlValue{0.0}), lower_bound(lower), upper_bound(upper), count(cnt), is_discrete(false) {}
};

/**
 * @brief Summary data for histogram statistics.
 */
struct HistogramSummary {
  std::vector<HistogramBin> bins;                   ///< Histogram bins with counts
  std::size_t               total_count{0};         ///< Total number of samples processed
  std::size_t               unique_values{0};       ///< Number of unique values (equals bins.size() for discrete)
  bool                      is_discrete{true};      ///< True if using discrete value bins, false for range bins
  std::size_t               out_of_range_count{0};  ///< Samples that fell outside the histogram range (for range bins)
};

/**
 * @brief Base interface for all summarizer implementations.
 */
class ISummarizer {
 public:
  virtual ~ISummarizer() = default;

  // Abstract base class - delete copy operations and default move operations
  ISummarizer(const ISummarizer&)            = delete;
  ISummarizer& operator=(const ISummarizer&) = delete;
  ISummarizer(ISummarizer&&)                 = default;
  ISummarizer& operator=(ISummarizer&&)      = default;

  ISummarizer() = default;

  /**
   * @brief Generate a summary from processed sample data.
   *
   * @param samples Span of processed samples to summarize
   * @return Summary result or error status
   */
  virtual auto Summarize(std::span<const ProcessedSampledData> samples) const
      -> std::expected<SummaryResult, astl_status_code> = 0;

  /**
   * @brief Get the type of summary this summarizer produces.
   *
   * @return String identifier for the summary type
   */
  virtual auto GetSummaryType() const -> std::string = 0;

  /**
   * @brief Check if this summarizer supports the given value type and metric type combination.
   *
   * @param value_type The ASTL value type to check
   * @param metric_type The ASTL metric type to check
   * @return true if the summarizer supports this combination, false otherwise
   */
  virtual auto IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const -> bool = 0;
};

/**
 * @brief Summarizer for min/max/average statistics.
 *
 * Computes minimum, maximum, and average values from numeric samples.
 * Only works with arithmetic types (integers, floats).
 */
class MinMaxAvgSummarizer : public ISummarizer {
 public:
  auto Summarize(std::span<const ProcessedSampledData> samples) const
      -> std::expected<SummaryResult, astl_status_code> override;

  auto GetSummaryType() const -> std::string override { return "MinMaxAvg"; }

  auto IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const -> bool override;
};

/**
 * @class HistogramSummarizer
 * @brief Histogram summarizer for statistical distribution analysis.
 * Provides two modes for histogram summarization: Discrete and Ranged.
 *
 * @todo(ASTL-225): Add support for configuring a Range Based Histogram based on user
 * configuration with a custom bin count
 * @todo(ASTL-228): Separate HistogramSummarizer into DiscreteHistogramSummarizer and
 * RangeHistogramSummarizer classes for clarity
 * @section histogram_modes Histogram Modes
 *
 * - Discrete Histogram:
 *   - Each bin represents a unique, exact value from the dataset.
 *   - Counts the frequency of each distinct value.
 *   - Best for categorical data, small integer ranges, or finite sets.
 *
 * - Ranged Histogram:
 *   - The value range is divided into equal-width intervals (bins).
 *   - Each bin counts how many values fall within its interval.
 *   - Best for continuous or wide-range numeric data (e.g., measurements, floating-point values).
 *
 * @section usage_guidance Usage Guidance
 *
 * - Use Discrete Histogram when:
 *   - Data is naturally grouped into distinct, countable categories or values.
 *   - You want to see the frequency of each unique value.
 *
 * - Use Ranged Histogram when:
 *   - Data is numeric and you want to understand its distribution across intervals.
 *   - The data is continuous or has many possible values.
 *
 * In summary:
 *   - Discrete = count per unique value (good for categories, small integers).
 *   - Ranged = count per interval (good for continuous or wide-range numeric data).
 */
class HistogramSummarizer : public ISummarizer {
 public:
  /**
   * @brief Construct a histogram summarizer for discrete value-based binning.
   *
   * In this mode, each unique value will get its own bin.
   */
  HistogramSummarizer() : use_discrete_bins_(true), num_bins_(0) {}

  /**
   * @brief Construct a histogram summarizer with fixed number of range bins.
   *
   * @param num_bins Number of equal-width bins to use for the histogram
   */
  explicit HistogramSummarizer(std::size_t num_bins) : use_discrete_bins_(false), num_bins_(num_bins) {}

  auto Summarize(std::span<const ProcessedSampledData> samples) const
      -> std::expected<SummaryResult, astl_status_code> override;

  auto GetSummaryType() const -> std::string override {
    return use_discrete_bins_ ? "DiscreteHistogram" : "RangeHistogram";
  }

  auto IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const -> bool override;

 private:
  bool        use_discrete_bins_;  ///< True for discrete value bins, false for range bins
  std::size_t num_bins_;           ///< Number of bins to use for range histogram (ignored for discrete)

  /**
   * @brief Generate histogram with discrete value-based bins.
   */
  // cppcheck-suppress unusedPrivateFunction
  auto SummarizeDiscrete(std::span<const ProcessedSampledData> samples) const  // NOLINT
      -> std::expected<SummaryResult, astl_status_code>;

  /**
   * @brief Generate histogram with fixed-width range bins.
   */
  // cppcheck-suppress unusedPrivateFunction
  auto SummarizeRanged(std::span<const ProcessedSampledData> samples) const  // NOLINT
      -> std::expected<SummaryResult, astl_status_code>;
};

}  // namespace astl

#endif  // ASTL_SUMMARIZER_HPP_
