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
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "astl_logger.hpp"        // ASTL_LOG_*
#include "common/astl_value.hpp"  // to_string
#include "csv_system_info.hpp"

namespace astl {

// TODO(https://jira.arm.com/browse/ASTL-211): Use ASTL::FileInterface instead of std::filesystem for file operations.
SummaryCsvOutput::SummaryCsvOutput(std::filesystem::path path)
    : SummaryOutput(CreateSummarizers()), _path(std::move(path)) {}

// Each output writer type decides which summarizers to use based on its format requirements
std::vector<std::unique_ptr<ISummarizer>> SummaryCsvOutput::CreateSummarizers() {
  std::vector<std::unique_ptr<ISummarizer>> summarizers;

  // Add MinMaxAvg summarizer
  summarizers.push_back(std::make_unique<MinMaxAvgSummarizer>());

  // Add discrete histogram summarizer (each unique value gets its own bin)
  summarizers.push_back(std::make_unique<HistogramSummarizer>());

  return summarizers;
}

// Generic grouper: groups (target, metric, summary) tuples by metric name.
template <typename SummaryT>
static auto GroupByMetricName(const std::vector<std::tuple<const ITarget*, const IMetric*, SummaryT>>& summaries)
    -> std::map<std::string, std::vector<std::tuple<const ITarget*, const IMetric*, SummaryT>>> {
  std::map<std::string, std::vector<std::tuple<const ITarget*, const IMetric*, SummaryT>>> summaries_by_name;
  for (const auto& [target, metric, summary] : summaries) {
    std::string metric_name = metric->Name();
    summaries_by_name[metric_name].emplace_back(target, metric, summary);
  }
  return summaries_by_name;
}

auto SummaryCsvOutput::WriteSummaries(
    const std::vector<std::tuple<const ITarget*, const IMetric*, SummaryResult>>& summaries) const -> astl_status_code {
  if (_path.empty()) {
    ASTL_LOG_WARNING("CSV output file path is empty");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  std::ofstream csv_file(_path, std::ios::trunc);
  if (!csv_file.is_open()) {
    ASTL_LOG_ERROR("Failed to open CSV file for writing: {}", _path.string());
    return ASTL_STATUS_FILE_ERROR;
  }

  WriteSystemInfoCsvSection(csv_file);

  // Separate summaries by type in the derived class (CSV-specific logic)
  std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>> minmax_summaries;
  std::vector<std::tuple<const ITarget*, const IMetric*, HistogramSummary>> histogram_summaries;

  for (const auto& [target, metric, summary] : summaries) {
    std::visit(
        [&](const auto& sum) {
          using T = std::decay_t<decltype(sum)>;
          if constexpr (std::is_same_v<T, MinMaxAvgSummary>) {
            minmax_summaries.emplace_back(target, metric, sum);
          } else if constexpr (std::is_same_v<T, HistogramSummary>) {
            histogram_summaries.emplace_back(target, metric, sum);
          }
        },
        summary);
  }

  // Write MinMaxAvg table
  if (!minmax_summaries.empty()) {
    csv_file << "Min/Max/Average Summary\n";
    csv_file << "MetricName,Target,Min,Max,Average,Count\n";

    auto minmax_by_name = GroupByMetricName(minmax_summaries);
    for (const auto& [metric_name, metric_summaries] : minmax_by_name) {
      for (const auto& [target, metric, summary] : metric_summaries) {
        WriteMinMaxAvgEntry(csv_file, metric_name, target, summary);
      }
    }
    csv_file << "\n";  // Blank line separator
  }

  // Write Histogram table
  if (!histogram_summaries.empty()) {
    csv_file << "Histogram Summary\n";
    csv_file << "MetricName,Target,Type,Value/Range,Count\n";

    auto histogram_by_name = GroupByMetricName(histogram_summaries);
    for (const auto& [metric_name, metric_summaries] : histogram_by_name) {
      for (const auto& [target, metric, summary] : metric_summaries) {
        WriteHistogramEntry(csv_file, metric_name, target, summary);
      }
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

auto SummaryCsvOutput::WriteMinMaxAvgEntry(std::ofstream& csv_file, const std::string& metric_name,
                                           const ITarget* target, const MinMaxAvgSummary& summary) -> void {
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

auto SummaryCsvOutput::WriteHistogramEntry(std::ofstream& csv_file, const std::string& metric_name,
                                           const ITarget* target, const HistogramSummary& summary) -> void {
  // Write one row per bin
  for (const auto& bin : summary.bins) {
    csv_file << metric_name << "," << target->Name() << ",";

    // Type column (Discrete or Range)
    csv_file << (summary.is_discrete ? "Discrete" : "Range") << ",";

    // Value/Range column
    if (summary.is_discrete) {
      // For discrete: show the exact value
      csv_file << to_string(bin.value);
    } else {
      // For range: show the bin range as [lower, upper)
      csv_file << "[" << bin.lower_bound << "," << bin.upper_bound << ")";
    }

    // Count column
    csv_file << "," << bin.count << "\n";
  }
}

}  // namespace astl
