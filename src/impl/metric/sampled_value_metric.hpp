// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SAMPLED_VALUE_METRIC_HPP_
#define SAMPLED_VALUE_METRIC_HPP_

#include <optional>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "raw_metric.hpp"

namespace astl {
/**
 * @brief Holds summary values for min, max, and average samples.
 * This structure is used to store computed statistics
 * TODO (https://jira.arm.com/browse/ASTL-99) : Add support for different summary types.
 * from sampled data in SampledValueMetric.
 */
struct MinMaxAvgSummaryData {
  std::optional<AstlValue> min;  ///< Minimum sample value seen
  std::optional<AstlValue> max;  ///< Maximum sample value seen
  std::optional<AstlValue> avg;  ///< Computed average of sample values
};

/**
 * @brief Base class interface for metrics that use sampled numerical data.
 *
 * SampledValueMetric provides common functionality such as accumulating
 * sample statistics and logging.
 */
class SampledValueMetric : public RawMetric {
 public:
  SampledValueMetric() = delete;
  /**
   * @brief Construct a SampledValueMetric with specified name, description, units, and value type.
   *
   * Initializes the metric with the provided parameters and sets up initial summary data.
   * TODO (https://jira.arm.com/browse/ASTL-97) : Add support for masks, formula to allow more complex processing of
   * metrics.
   *
   * @param configuration The configuration for the metric, including name, units, and how to build operations
   * @param target The telemetry source for the metric.
   * @param processed_sample_sink Output for where processed samples should be sent.
   */
  explicit SampledValueMetric(const MetricConfig *configuration, const ITarget *target,
                              IProcessedSampleSink *processed_sample_sink);

  /**
   * @brief Process and record a new sample value.
   *
   * Updates the internal state by incorporating the given sample
   * into min, max, sum, and count statistics.
   *
   * @param sample A single sampled data point to be processed.
   * @return astl_status_code indicating success or failure.
   */
  auto ReceiveRawSample(const NormalizedSampledData &raw_sample) -> astl_status_code override;

  /**
   * @brief Reset the metric state, dropping all collected samples
   */
  void Reset() override;

  /**
   * @brief Summarize collected sample data.
   *
   * Finalizes the summary by calculating the average value from
   * accumulated samples. Logs the results using the summary logger.
   *
   * @return astl_status_code indicating success or failure.
   */
  auto Summarize() -> astl_status_code override;

  /**
   * @brief Retrieve the statistical summary of the sampled values.
   *
   * Returns the current summary data containing minimum, maximum,
   * and average values computed from received samples.
   *
   * @return A SampledValueSummaryData struct with summary statistics.
   */
  auto GetSummaryData() const -> MinMaxAvgSummaryData;

 private:
  /** @brief private helper to update statistics for summary later */
  auto UpdateStatistics(const ProcessedSampledData &processed_sample) -> astl_status_code;

  // private helper to initialize or reset the samples + statistics
  void InitializeSamples();

  std::vector<ProcessedSampledData> _processed_samples;
  mutable std::mutex                _samples_mutex;  // protects _processed_samples & summary data
  MinMaxAvgSummaryData              _summary_data;   // Summary data for min, max, avg
  // Sum of sample values for average calculation. Uses max representation to reduce risk of overflow.
  // uint64_t or double are the largest natively support integer/float type across Windows, macOS and Linux.
  AstlValue _sum_sample_value;
  // create a logger instance to explicitly log summary
  astl::Logger _summary_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                               false /* No default formatting */, "sampled_value_summary.log"};

};  // End of SampledValueMetric class

}  // namespace astl

#endif  // SAMPLED_VALUE_METRIC_HPP_
