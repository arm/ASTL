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

/**
 * @file summary_output.hpp
 * @brief Base class for output classes that generate statistical summaries of processed samples.
 *
 * Core behaviors:
 *  - Generates min/max/average statistics for all processed samples using summarizers
 *  - Uses CanHandle() to validate that summarizers can process each metric type
 *  - Skips metrics that cannot be summarized rather than failing completely
 *  - Provides a pure virtual method for concrete classes to implement output writing
 *
 * This class handles all the summary computation logic, while concrete derived classes
 * handle the specific output format (CSV, JSON, etc.).
 */
#ifndef SUMMARY_OUTPUT_HPP_
#define SUMMARY_OUTPUT_HPP_

#include <tuple>
#include <vector>

#include "astl/astl_errors.h"
#include "common/astl_defines.hpp"
#include "output/i_output.hpp"
#include "output/summarizer.hpp"

namespace astl {

/**
 * @brief Base class for output classes that generate statistical summaries.
 *
 * This abstract class implements the IOutput interface to provide common summary generation
 * functionality. It handles the grouping of metrics, computation of statistics using
 * summarizers, and delegates the actual output writing to derived classes.
 */
class SummaryOutput : public IOutput {
 public:
  /**
   * @brief Constructor with a list of summarizers.
   *
   * @param summarizers Vector of unique_ptr to ISummarizer implementations
   */
  explicit SummaryOutput(std::vector<std::unique_ptr<ISummarizer>> summarizers);

  SummaryOutput(const SummaryOutput&)                    = delete;
  auto operator=(const SummaryOutput&) -> SummaryOutput& = delete;
  SummaryOutput(SummaryOutput&&)                         = default;
  auto operator=(SummaryOutput&&) -> SummaryOutput&      = default;
  ~SummaryOutput() override                              = default;

  using IOutput::WriteProcessedSamples;  // bring other overloads into scope to avoid -Woverloaded-virtual

  /**
   * @brief Generate and write summary statistics for all processed samples.
   *
   * Groups all metrics by name, computes min/max/average/count for each target-metric
   * combination, and delegates the actual writing to the derived class implementation.
   *
   * @param processed Nested map Target* -> Metric* -> vector<ProcessedSampledData>
   * @return astl_status_code Success, or error from validation/computation/writing
   */
  auto WriteProcessedSamples(const ProcessedSamplesMap& samples) const -> astl_status_code override;

 protected:
  /**
   * @brief Pure virtual method for derived classes to implement output writing.
   *
   * This method is called by WriteProcessedSamples after all summary computation is complete.
   * Derived classes should implement this to write the summaries in their specific format.
   * The derived class can organize the data as needed (e.g., by metric name for CSV).
   *
   * @param summaries Vector of (target, metric, summary) tuples for all computed summaries
   * @return astl_status_code Success or error from writing operation
   */
  virtual auto WriteSummaries(const std::vector<std::tuple<const ITarget*, const IMetric*, SummaryResult>>& summaries)
      const -> astl_status_code = 0;

 private:
  /**
   * @brief Compute summary for a single metric's samples.
   * @param target The target this metric belongs to
   * @param metric The metric to summarize
   * @param sample_data The processed samples for this metric
   * @return Optional summary if computation succeeds, nullopt otherwise
   */
  auto ComputeSummariesForMetric(const ITarget* target, const IMetric* metric,
                                 std::span<const ProcessedSampledData> sample_data) const -> std::vector<SummaryResult>;

  std::vector<std::unique_ptr<ISummarizer>> summarizers_;  ///< List of summarizers to apply to metrics
};

}  // namespace astl

#endif  // SUMMARY_OUTPUT_HPP_
