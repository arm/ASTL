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

#ifndef FINITE_SET_SAMPLED_VALUE_METRIC_HPP_
#define FINITE_SET_SAMPLED_VALUE_METRIC_HPP_

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "sampled_value_metric.hpp"

namespace astl {

/**
 * @brief Structure to hold count statistics for finite set values.
 *
 * Contains the count of occurrences for each value in the finite set.
 * Values are tracked by their AstlValue representation.
 */
struct FiniteSetSummaryData {
  std::map<AstlValue, uint64_t> value_counts;        ///< Count of each value occurrence in the finite set
  uint64_t                      total_samples  = 0;  ///< Total number of samples processed
  uint64_t                      unknown_values = 0;  ///< Count of values not in the finite set
};

/**
 * @brief Sampled value metric for handling finite sets of known values.
 *
 * This class extends SampledValueMetric to handle scenarios where the metric
 * can only take on a finite set of known AstlValue objects. The class automatically
 * tracks which values are in the predefined finite set and which are unknown.
 *
 * Examples:
 * - System states: {AstlValue{0U}, AstlValue{1U}, AstlValue{2U}} for Idle/Active/Sleep
 * - CPU C-states: {AstlValue{0U}, AstlValue{1U}, AstlValue{6U}, AstlValue{7U}}
 * - Power levels: {AstlValue{1.0}, AstlValue{2.5}, AstlValue{5.0}}
 * - String states: {AstlValue{std::string{"ON"}}, AstlValue{std::string{"OFF"}}}
 */
class FiniteSetMetric : public SampledValueMetric {
 public:
  using FiniteSet    = std::set<AstlValue>;
  using ValueToLabel = std::map<AstlValue, std::string>;

  FiniteSetMetric() = delete;

  /**
   * @brief Construct a FiniteSetMetric with specified parameters and finite set.
   *
   * @param configuration The configuration for the metric, including name, units, and how to build operations
   * @param finite_set The set of valid AstlValue objects that define the finite set.
   * @param labels Optional mapping from finite set values to human-readable labels.
   * @param target The target associated with this metric.
   * @param processed_sample_sink The sample_sink for processed samples.
   */
  explicit FiniteSetMetric(const FiniteSetMetricConfig *configuration, const ITarget *target,
                           IProcessedSampleSink *processed_sample_sink);

  /**
   * @brief Process and record a new sample value.
   *
   * Checks if the received sample is in the predefined finite set and
   * updates occurrence counts accordingly. Values not in the finite set
   * are tracked as unknown values.
   *
   * @param raw_sample A single raw sampled data point to be processed.
   * @return astl_status_code indicating success or failure.
   */
  auto ReceiveRawSample(const RawSampledData &raw_sample) -> astl_status_code override;

  /**
   * @brief Reset the metric state, dropping all collected samples and counts.
   */
  void Reset() override;

  /**
   * @brief Summarize collected sample data for finite set values.
   *
   * Generates a summary containing:
   * - Count for each value found in the data stream
   * - Percentage distribution of each value in the finite set
   * - List of any unknown values encountered
   *
   * @return astl_status_code indicating success or failure.
   */
  auto Summarize() -> astl_status_code override;

  /**
   * @brief Retrieve the finite set summary statistics.
   *
   * @return A FiniteSetSummaryData struct with occurrence counts and statistics.
   */
  FiniteSetSummaryData GetFiniteSetSummaryData() const { return _finite_set_summary; }

  /**
   * @brief Check if a value is in the predefined finite set.
   *
   * @param value The AstlValue to check.
   * @return true if the value is in the finite set, false otherwise.
   */
  bool IsInFiniteSet(const AstlValue &value) const { return _finite_set_configuration->IsInFiniteSet(value); }

  /**
   * @brief Get the finite set of valid values.
   *
   * @return The complete set of valid AstlValue objects.
   */
  const std::set<AstlValue> &GetFiniteSet() const { return _finite_set_configuration->GetFiniteSet(); }

 private:
  /** @brief Update finite set statistics for the received sample */
  auto UpdateFiniteSetStatistics(const RawSampledData &raw_sample) -> astl_status_code;

  /** @brief Log detailed finite set summary information */
  void LogFiniteSetSummary();

  FiniteSetMetricConfig const *_finite_set_configuration{nullptr};
  FiniteSetSummaryData         _finite_set_summary;  ///< Summary statistics for finite set values

  // Create a logger instance to explicitly log finite set summary
  astl::Logger _finite_summary_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                                      false /* No default formatting */, "finite_set_summary.log"};

};  // End of FiniteSetMetric class

}  // namespace astl

#endif  // FINITE_SET_SAMPLED_VALUE_METRIC_HPP_
