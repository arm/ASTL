// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "summary_csv_output.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "astl_logger.hpp"  // ASTL_LOG_*
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

  summarizers.push_back(std::make_unique<MinMaxAvgSummarizer>());
  summarizers.push_back(std::make_unique<TimeWeightedAvgSummarizer>());
  summarizers.push_back(std::make_unique<HistogramSummarizer>());

  return summarizers;
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

  WriteCollectionInfoCsvSection(csv_file);

  auto [minmax, histogram, twa] = PartitionSummaries(summaries);

  WriteCombinedStatsSection(csv_file, minmax, histogram, twa);

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
    const std::vector<std::tuple<const ITarget*, const IMetric*, HistogramSummary>>&       histogram,
    const std::vector<std::tuple<const ITarget*, const IMetric*, TimeWeightedAvgSummary>>& twa) -> void {
  auto metric_sections = BuildMetricSections(minmax, histogram, twa);
  if (metric_sections.empty()) {
    return;
  }

  for (auto& [metric_name, entries_by_target] : metric_sections) {
    const auto first_entry = std::find_if(entries_by_target.begin(), entries_by_target.end(),
                                          [](const auto& item) { return item.second.metric != nullptr; });
    if (first_entry == entries_by_target.end()) {
      continue;
    }

    WriteMetricHeader(csv_file, metric_name, first_entry->second.metric);
    csv_file << "\nSummary Statistics\n";
    csv_file << "Target,Min,Max,Average,Time Weighted Average,Count\n";
    for (const auto& [target_name, entry] : entries_by_target) {
      (void)target_name;
      WriteCombinedStatsEntry(csv_file, entry);
    }
    WriteHistogramSection(csv_file, metric_name, entries_by_target);
  }
}

auto SummaryCsvOutput::BuildMetricSections(
    const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>&       minmax,
    const std::vector<std::tuple<const ITarget*, const IMetric*, HistogramSummary>>&       histogram,
    const std::vector<std::tuple<const ITarget*, const IMetric*, TimeWeightedAvgSummary>>& twa)
    -> std::map<std::string, std::map<std::string, MetricTargetSummary>> {
  std::map<std::string, std::map<std::string, MetricTargetSummary>> metric_sections;

  const auto ensure_entry = [&metric_sections](const ITarget* target, const IMetric* metric) -> MetricTargetSummary& {
    auto& entry  = metric_sections[metric->Name()][target->Name()];
    entry.target = target;
    entry.metric = metric;
    return entry;
  };

  for (const auto& [target, metric, summary] : minmax) {
    ensure_entry(target, metric).minmax = summary;
  }

  for (const auto& [target, metric, summary] : histogram) {
    ensure_entry(target, metric).histogram = summary;
  }

  for (const auto& [target, metric, summary] : twa) {
    ensure_entry(target, metric).twa = summary;
  }

  return metric_sections;
}

auto SummaryCsvOutput::WriteCombinedStatsEntry(std::ofstream& csv_file, const MetricTargetSummary& entry) -> void {
  std::size_t count = 0;
  if (entry.minmax.has_value()) {
    count = entry.minmax->count;
  } else if (entry.twa.has_value()) {
    count = entry.twa->count;
  } else if (entry.histogram.has_value()) {
    count = entry.histogram->total_count;
  }

  csv_file << (entry.target != nullptr ? entry.target->Name() : std::string{"<unknown target>"}) << ",";
  csv_file << (entry.minmax.has_value() && entry.minmax->min.has_value() ? FormatReportValue(entry.minmax->min.value())
                                                                         : "N/A")
           << ",";
  csv_file << (entry.minmax.has_value() && entry.minmax->max.has_value() ? FormatReportValue(entry.minmax->max.value())
                                                                         : "N/A")
           << ",";
  csv_file << (entry.minmax.has_value() && entry.minmax->avg.has_value() ? FormatReportValue(entry.minmax->avg.value())
                                                                         : "N/A")
           << ",";
  csv_file << (entry.twa.has_value() && entry.twa->time_weighted_avg.has_value()
                   ? FormatReportValue(entry.twa->time_weighted_avg.value())
                   : "N/A")
           << ",";
  csv_file << count << "\n";
}

auto SummaryCsvOutput::WriteHistogramSection(std::ofstream& csv_file, const std::string& metric_name,
                                             const std::map<std::string, MetricTargetSummary>& entries_by_target)
    -> void {
  (void)metric_name;

  struct TargetHistogramRow {
    std::string                        target_name;
    std::map<std::string, std::size_t> counts_by_value;
  };

  std::vector<TargetHistogramRow> target_rows;
  std::vector<std::string>        distinct_values;
  std::set<std::string>           seen_values;

  for (const auto& [target_name, entry] : entries_by_target) {
    (void)target_name;
    if (!entry.histogram.has_value()) {
      continue;
    }
    const auto& summary = entry.histogram.value();
    if (!summary.is_discrete || summary.bins.empty()) {
      continue;
    }

    TargetHistogramRow target_row{
        .target_name     = entry.target != nullptr ? entry.target->Name() : std::string{"<unknown target>"},
        .counts_by_value = {}};
    for (const auto& bin : summary.bins) {
      const auto value = FormatReportValue(bin.value);
      target_row.counts_by_value[value] += bin.count;
      if (seen_values.insert(value).second) {
        distinct_values.push_back(value);
      }
    }
    target_rows.push_back(std::move(target_row));
  }

  if (target_rows.empty() || distinct_values.empty()) {
    csv_file << "\n";
    return;
  }

  csv_file << "\nHistogram Summary\n";
  csv_file << "Target,Type";
  for (const auto& value : distinct_values) {
    csv_file << "," << value;
  }
  csv_file << "\n";

  std::ranges::sort(target_rows, [](const TargetHistogramRow& left, const TargetHistogramRow& right) {
    return left.target_name < right.target_name;
  });

  for (const auto& target_row : target_rows) {
    csv_file << target_row.target_name << ",Discrete";
    for (const auto& value : distinct_values) {
      const auto count_it = target_row.counts_by_value.find(value);
      const auto count    = (count_it != target_row.counts_by_value.end()) ? count_it->second : 0;
      csv_file << "," << count;
    }
    csv_file << "\n";
  }

  csv_file << "\n";
}

}  // namespace astl
