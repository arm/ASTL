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
 * @file csv_summary_output.hpp
 * @brief Writes processed metric samples summaries to a CSV file.
 *
 * Core behaviors:
 *  - Inherits from SummaryOutput to get summary computation functionality
 *  - Outputs CSV format: MetricName,Target,Min,Max,Average,SampleCount
 *  - Environment variable `ASTL_CSV_OUTPUT_FILE` selects the output file path
 *  - Overwrites existing files (truncate mode) to ensure clean output
 *
 * Schema example:
 *   MetricName,Target,Min,Max,Average,SampleCount
 *   Temperature,Target1,20.1,35.7,27.84,150
 *   Temperature,Target2,18.5,33.2,25.91,148
 *   Voltage,Target1,3.25,3.35,3.30,150
 */
#ifndef SUMMARY_CSV_OUTPUT_HPP_
#define SUMMARY_CSV_OUTPUT_HPP_

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "astl/astl_errors.h"
#include "common/astl_defines.hpp"    // ProcessedSamplesMap, ITarget, IMetric
#include "output/summarizer.hpp"      // MinMaxAvgSummary
#include "output/summary_output.hpp"  // Base class

namespace astl {

/**
 * @brief CSV summary output class that writes statistical summaries to CSV files.
 *
 * This class inherits from SummaryOutput to get summary computation functionality
 * and implements the CSV-specific output writing logic.
 */
class SummaryCsvOutput : public SummaryOutput {
 public:
  /**
   * @brief Construct a CSV summary output with the specified file path.
   * @param path The filesystem path where the CSV file will be written.
   */
  explicit SummaryCsvOutput(std::filesystem::path path);

  SummaryCsvOutput(const SummaryCsvOutput&)                    = delete;
  auto operator=(const SummaryCsvOutput&) -> SummaryCsvOutput& = delete;
  SummaryCsvOutput(SummaryCsvOutput&&)                         = default;
  auto operator=(SummaryCsvOutput&&) -> SummaryCsvOutput&      = default;
  ~SummaryCsvOutput() override                                 = default;

  using IOutput::WriteProcessedSamples;  // bring other overloads into scope to avoid -Woverloaded-virtual

  /**
   * @brief Whether the output is available for writing.
   * @return true if the file path is valid and writable.
   */
  auto Ready() const -> bool { return !_path.empty(); }

 protected:
  /**
   * @brief Write summary statistics to CSV file.
   *
   * Implements the WriteSummaries method from SummaryOutput to write
   * the computed summaries in CSV format. Groups summaries by metric name
   * for organized CSV output.
   *
   * @param summaries Vector of (target, metric, summary) tuples for all computed summaries
   * @return astl_status_code Success, file error, or internal error
   */
  auto WriteSummaries(const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>& summaries) const
      -> astl_status_code override;

 private:
  std::filesystem::path _path;

  /**
   * @brief Group summaries by metric name for organized CSV output.
   * @param summaries Vector of (target, metric, summary) tuples
   * @return Map of metric names to vectors of (target, metric, summary) tuples
   */
  static auto GroupSummariesByMetricName(
      const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>& summaries)
      -> std::map<std::string, std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>>;

  /**
   * @brief Write a single summary entry to the CSV file.
   * @param csv_file The output stream to write to
   * @param metric_name The name of the metric
   * @param target The target this summary applies to
   * @param summary The computed summary statistics
   */
  static auto WriteSummaryEntry(std::ofstream& csv_file, const std::string& metric_name, const ITarget* target,
                                const MinMaxAvgSummary& summary) -> void;

  /**
   * @brief Write the CSV header row.
   * @param csv_file The output stream to write to
   */
  static auto WriteHeader(std::ofstream& csv_file) -> void;
};

}  // namespace astl

#endif  // SUMMARY_CSV_OUTPUT_HPP_