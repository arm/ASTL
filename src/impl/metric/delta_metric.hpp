// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef DELTA_METRIC_HPP_
#define DELTA_METRIC_HPP_

#include <expected>
#include <optional>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "raw_metric.hpp"

namespace astl {

/**
 * @brief Holds summary values for delta statistics.
 * This structure is used to store computed delta statistics.
 * // TODO (ASTL-58): Various summary types will be implemented in the OutputManager.
 */
struct DeltaSummaryData {
  std::optional<AstlValue> min_delta;  ///< Minimum delta value seen
  std::optional<AstlValue> max_delta;  ///< Maximum delta value seen
  std::optional<AstlValue> avg_delta;  ///< Computed average of delta values
};

/**
 * @brief Delta metric class that calculates differences between consecutive samples.
 *
 * DeltaMetric processes sampled data and computes the delta (difference) between
 * consecutive samples. It maintains statistics about the deltas and can be used
 * as a base class for more complex metrics like RateMetric.
 */
class DeltaMetric : public RawMetric {
 public:
  DeltaMetric() = delete;

  /**
   * @brief Construct a DeltaMetric with specified name, description, units, and value type.
   *
   * Initializes the metric with the provided parameters and sets up initial delta tracking.
   *
   * @param configuration The non-owned pointer to configuration for the metric, including name, units, and how to build
   * operations
   * @param target The telemetry source for the metric.
   * @param processed_sample_sink Output for where processed samples should be sent.
   */
  explicit DeltaMetric(const MetricConfig* configuration, const ITarget* target,
                       IProcessedSampleSink* processed_sample_sink);

  /**
   * @brief Process and record a new sample value, calculating delta from previous sample.
   *
   * Updates the internal state by calculating the delta between this sample and the
   * previous sample, then incorporates it into delta statistics.
   *
   * @param sample A single sampled data point to be processed.
   * @return astl_status_code indicating success or failure.
   */
  auto ReceiveRawSample(const RawSampledData& raw_sample) -> astl_status_code override;

  /**
   * @brief Summarize collected delta data.
   *
   * Finalizes the summary by calculating statistics from accumulated deltas.
   * Logs the results using the summary logger.
   *
   * @return astl_status_code indicating success or failure.
   */
  auto Summarize() -> astl_status_code override;

  /**
   * @brief Retrieve the delta summary data.
   *
   * Returns the current delta summary data containing minimum, maximum,
   * and average delta values computed from received samples.
   *
   * @return A DeltaSummaryData struct with delta statistics.
   */
  auto GetDeltaSummaryData() const -> DeltaSummaryData;

  /**
   * @brief Reset the metric state, dropping all collected samples
   */
  auto Reset() -> void override;

 protected:
  /**
   * @brief Initialize/reset delta samples and summary data.
   *
   * Resets the metric state by clearing all delta data and reinitializing
   * the summary data structures.
   */
  auto InitializeSamples() -> void;

  /**
   * @brief Calculate delta between current and previous sample.
   *
   * @param current_sample The current sample value.
   * @param previous_sample The previous sample value.
   * @return Expected delta value or error code.
   */
  static auto CalculateDelta(const AstlValue& current_sample, const AstlValue& previous_sample)
      -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Update delta statistics with a new delta value.
   *
   * @param delta_value The new delta value to incorporate.
   * @param timestamp The timestamp when the delta was calculated.
   * @return astl_status_code indicating success or failure.
   */
  auto UpdateDeltaStatistics(const AstlValue& delta_value) -> astl_status_code;

  // NOLINTBEGIN - Disable clang-tidy checks for protected members - required by RateMetric class inherited from
  // DeltaMetric
  std::optional<RawSampledData> _previous_sample;               // Previous sample for delta calculation
  DeltaSummaryData              _delta_summary_data;            // Summary data for delta statistics
  AstlValue                     _sum_delta_value{uint64_t{0}};  // Sum of delta values for average calculation
  uint64_t                      _delta_count{0};                // Number of deltas processed

  // NOLINTEND
 private:
  // Create a logger instance to explicitly log delta summaries
  astl::Logger _delta_summary_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                                     false /* No default formatting */, "delta_summary.log"};
};

}  // namespace astl

#endif  // DELTA_METRIC_HPP_
