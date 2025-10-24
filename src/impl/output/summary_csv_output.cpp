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

#include "summary_csv_output.hpp"

#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "astl_logger.hpp"        // ASTL_LOG_*
#include "common/astl_value.hpp"  // to_string

namespace astl {

// TODO(https://jira.arm.com/browse/ASTL-211): Use ASTL::FileInterface instead of std::filesystem for file operations.
SummaryCsvOutput::SummaryCsvOutput(std::filesystem::path path) : _path(std::move(path)) {}

auto SummaryCsvOutput::WriteSummaries(
    const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>& summaries) const
    -> astl_status_code {
  if (_path.empty()) {
    ASTL_LOG_WARNING("CSV output file path is empty");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  std::ofstream csv_file(_path, std::ios::trunc);
  if (!csv_file.is_open()) {
    ASTL_LOG_ERROR("Failed to open CSV file for writing: {}", _path.string());
    return ASTL_STATUS_FILE_ERROR;
  }

  WriteHeader(csv_file);

  // Group summaries by metric name for organized CSV output
  auto summaries_by_name = GroupSummariesByMetricName(summaries);

  for (const auto& [metric_name, metric_summaries] : summaries_by_name) {
    for (const auto& [target, metric, summary] : metric_summaries) {
      WriteSummaryEntry(csv_file, metric_name, target, summary);
    }
  }

  csv_file.close();
  if (csv_file.fail()) {
    ASTL_LOG_ERROR("Error occurred while writing to CSV file: {}", _path.string());
    return ASTL_STATUS_FILE_ERROR;
  }

  ASTL_LOG_INFO("CSV summary written to: {}", _path.string());
  return ASTL_STATUS_SUCCESS;
}

auto SummaryCsvOutput::GroupSummariesByMetricName(
    const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>& summaries)
    -> std::map<std::string, std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>> {
  std::map<std::string, std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>> summaries_by_name;

  for (const auto& [target, metric, summary] : summaries) {
    std::string metric_name = metric->Name();
    summaries_by_name[metric_name].emplace_back(target, metric, summary);
  }

  return summaries_by_name;
}

auto SummaryCsvOutput::WriteSummaryEntry(std::ofstream& csv_file, const std::string& metric_name, const ITarget* target,
                                         const MinMaxAvgSummary& summary) -> void {
  csv_file << metric_name << "," << target->Name() << ",";

  // Min value
  csv_file << (summary.min.has_value() ? to_string(summary.min.value()) : "N/A") << ",";

  // Max value
  csv_file << (summary.max.has_value() ? to_string(summary.max.value()) : "N/A") << ",";

  // Average value
  csv_file << (summary.avg.has_value() ? to_string(summary.avg.value()) : "N/A") << ",";

  // Sample count
  csv_file << summary.count << "\n";
}

auto SummaryCsvOutput::WriteHeader(std::ofstream& csv_file) -> void {
  csv_file << "MetricName,Target,Min,Max,Average,SampleCount\n";
}

}  // namespace astl