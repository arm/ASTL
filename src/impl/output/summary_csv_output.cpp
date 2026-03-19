// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "summary_csv_output.hpp"

#include <algorithm>
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

namespace {

auto WriteMetricHeader(std::ofstream& csv_file, const std::string& metric_name, const IMetric* metric) -> void {
  astl_metric_props_t props{};
  props.size           = sizeof(astl_metric_props_t);
  const bool has_props = metric != nullptr && metric->GetProperties(&props) == ASTL_STATUS_SUCCESS;

  csv_file << "Metric: " << metric_name;
  if (has_props && props.description != nullptr && !std::string_view{props.description}.empty()) {
    csv_file << " - " << props.description;
  }
  if (has_props) {
    csv_file << " (" << UnitsToString(props.units) << ")";
  } else {
    csv_file << " (Unknown)";
  }
  csv_file << "\n";
}

}  // namespace

// TODO(https://jira.arm.com/browse/ASTL-211): Use ASTL::FileInterface instead of std::filesystem for file operations.
SummaryCsvOutput::SummaryCsvOutput(std::filesystem::path path)
    : SummaryOutput(CreateSummarizers()), _path(std::move(path)) {}

// Each output writer type decides which summarizers to use based on its format requirements
std::vector<std::unique_ptr<ISummarizer>> SummaryCsvOutput::CreateSummarizers() {
  std::vector<std::unique_ptr<ISummarizer>> summarizers;

  // Add MinMaxAvg summarizer
  summarizers.push_back(std::make_unique<MinMaxAvgSummarizer>());

  // Add time-weighted average summarizer
  summarizers.push_back(std::make_unique<TimeWeightedAvgSummarizer>());

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

template <typename SummaryT>
static auto SortMetricEntriesByTargetName(std::vector<std::tuple<const ITarget*, const IMetric*, SummaryT>>& entries)
    -> void {
  std::ranges::sort(entries, [](const auto& left, const auto& right) {
    return std::get<0>(left)->Name() < std::get<0>(right)->Name();
  });
}

// Writes one CSV section with one table per metric and target-oriented rows.
template <typename SummaryT, typename WriterFn>
static auto WriteSection(std::ofstream& csv_file, const std::string& section_header, const std::string& column_header,
                         const std::vector<std::tuple<const ITarget*, const IMetric*, SummaryT>>& entries,
                         WriterFn                                                                 writer) -> void {
  if (entries.empty()) {
    return;
  }
  csv_file << section_header << "\n";
  auto by_name = GroupByMetricName(entries);
  for (auto& [metric_name, metric_entries] : by_name) {
    SortMetricEntriesByTargetName(metric_entries);
    WriteMetricHeader(csv_file, metric_name, std::get<1>(metric_entries.front()));
    csv_file << column_header << "\n";
    for (const auto& [target, metric, summary] : metric_entries) {
      writer(csv_file, metric_name, target, summary);
    }
    csv_file << "\n";
  }
}

// Partitioned view of a SummaryResult collection split by concrete type.
struct PartitionedSummaries {
  std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>       minmax;
  std::vector<std::tuple<const ITarget*, const IMetric*, HistogramSummary>>       histogram;
  std::vector<std::tuple<const ITarget*, const IMetric*, TimeWeightedAvgSummary>> twa;
};

// Dispatch each SummaryResult variant into the appropriate typed bucket.
static auto PartitionSummaries(const std::vector<std::tuple<const ITarget*, const IMetric*, SummaryResult>>& summaries)
    -> PartitionedSummaries {
  PartitionedSummaries result;
  for (const auto& [target, metric, summary] : summaries) {
    std::visit(
        [&](const auto& sum) {
          using T = std::decay_t<decltype(sum)>;
          if constexpr (std::is_same_v<T, MinMaxAvgSummary>) {
            result.minmax.emplace_back(target, metric, sum);
          } else if constexpr (std::is_same_v<T, HistogramSummary>) {
            result.histogram.emplace_back(target, metric, sum);
          } else if constexpr (std::is_same_v<T, TimeWeightedAvgSummary>) {
            result.twa.emplace_back(target, metric, sum);
          }
        },
        summary);
  }
  return result;
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

  auto [minmax, histogram, twa] = PartitionSummaries(summaries);

  WriteCombinedStatsSection(csv_file, minmax, twa);
  WriteSection(csv_file, "Histogram Summary", "Target,Type,Value/Range,Count", histogram, WriteHistogramEntry);

  csv_file.close();
  if (csv_file.fail()) {
    ASTL_LOG_ERROR("Error occurred while writing to CSV file: {}", _path.string());
    return ASTL_STATUS_FILE_ERROR;
  }

  ASTL_LOG_INFO("CSV summary written to: {}", _path.string());
  return ASTL_STATUS_SUCCESS;
}

auto SummaryCsvOutput::WriteCombinedStatsSection(
    std::ofstream& csv_file, const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>& minmax,
    const std::vector<std::tuple<const ITarget*, const IMetric*, TimeWeightedAvgSummary>>& twa) -> void {
  if (minmax.empty()) {
    return;
  }
  // Build (metric_name, target_name) → time_weighted_avg lookup from TWA results
  std::map<std::pair<std::string, std::string>, std::optional<AstlValue>> twa_lookup;
  for (const auto& [target, metric, summary] : twa) {
    twa_lookup[{metric->Name(), target->Name()}] = summary.time_weighted_avg;
  }
  csv_file << "Min/Max/Average Summary\n";
  auto by_name = GroupByMetricName(minmax);
  for (auto& [metric_name, entries] : by_name) {
    SortMetricEntriesByTargetName(entries);
    WriteMetricHeader(csv_file, metric_name, std::get<1>(entries.front()));
    csv_file << "Target,Min,Max,Average,TimeWeightedAvg,Count\n";
    for (const auto& [target, metric, summary] : entries) {
      auto       it     = twa_lookup.find({metric_name, target->Name()});
      const auto tw_avg = (it != twa_lookup.end()) ? it->second : std::optional<AstlValue>{};
      WriteCombinedStatsEntry(csv_file, metric_name, target, summary, tw_avg);
    }
    csv_file << "\n";
  }
}

auto SummaryCsvOutput::WriteCombinedStatsEntry(std::ofstream& csv_file, const std::string& metric_name,
                                               const ITarget* target, const MinMaxAvgSummary& summary,
                                               const std::optional<AstlValue>& tw_avg) -> void {
  (void)metric_name;
  csv_file << target->Name() << ",";

  // Min value
  csv_file << (summary.min.has_value() ? to_string(summary.min.value()) : "N/A") << ",";

  // Max value
  csv_file << (summary.max.has_value() ? to_string(summary.max.value()) : "N/A") << ",";

  // Average value
  csv_file << (summary.avg.has_value() ? to_string(summary.avg.value()) : "N/A") << ",";

  // Time-weighted average value
  csv_file << (tw_avg.has_value() ? to_string(tw_avg.value()) : "N/A") << ",";

  // Sample count
  csv_file << summary.count << "\n";
}

auto SummaryCsvOutput::WriteHistogramEntry(std::ofstream& csv_file, const std::string& metric_name,
                                           const ITarget* target, const HistogramSummary& summary) -> void {
  (void)metric_name;
  // Write one row per bin
  for (const auto& bin : summary.bins) {
    csv_file << target->Name() << ",";

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
