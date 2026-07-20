// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file csv_summary_output.hpp
 * @brief Writes processed metric samples summaries to a CSV file.
 *
 * Core behaviors:
 *  - Inherits from SummaryOutput to get summary computation functionality
 *  - Produces a CSV file with collection metadata followed by one
 *    per-metric section containing:
 *      1. Summary Statistics
 *      2. Histogram Summary (when supported)
 *  - Environment variable `ASTL_OUTPUT_SUMMARY_CSV` selects the output file path
 *  - Overwrites existing files (truncate mode) to ensure clean output
 *
 * Schema example (Min/Max/Average section):
 *   Metric: Temperature
 *   Target,Min,Max,Average,TimeWeightedAvg,Count
 *   Target1,20.1,35.7,27.84,25.3,150
 */
#ifndef SUMMARY_CSV_OUTPUT_HPP_
#define SUMMARY_CSV_OUTPUT_HPP_

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
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
   * the computed summaries in CSV format. Groups summaries into one table per
   * metric so multi-target collections remain easy to read.
   *
   * @param summaries Vector of (target, metric, summary) tuples for all computed summaries
   * @return astl_status_code Success, file error, or internal error
   */
  auto WriteSummaries(const std::vector<std::tuple<const ITarget*, const IMetric*, SummaryResult>>& summaries) const
      -> astl_status_code override;

 private:
  struct MetricTargetSummary {
    const ITarget*                        target{nullptr};
    const IMetric*                        metric{nullptr};
    std::optional<MinMaxAvgSummary>       minmax;
    std::optional<HistogramSummary>       histogram;
    std::optional<TimeWeightedAvgSummary> twa;
  };

  std::filesystem::path _path;

  /**
   * @brief Create the default set of summarizers.
   * @return Vector of default summarizers (MinMaxAvg, TimeWeightedAvg, DiscreteHistogram)
   */
  static auto CreateSummarizers() -> std::vector<std::unique_ptr<ISummarizer>>;

  /**
   * @brief Write the combined Min/Max/Average + TimeWeightedAvg section to the CSV file.
   *
   * Builds a lookup from (metric_name, target_name) → time_weighted_avg using @p twa, then
   * writes every MinMaxAvg entry with the corresponding TWA column ("N/A" when absent).
   *
   * @param csv_file  The output stream to write to
   * @param minmax    MinMaxAvg summarizer results
   * @param twa       TimeWeightedAvg summarizer results (may be empty)
   */
  static auto WriteCombinedStatsSection(
      std::ofstream& csv_file, const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>& minmax,
      const std::vector<std::tuple<const ITarget*, const IMetric*, HistogramSummary>>&       histogram,
      const std::vector<std::tuple<const ITarget*, const IMetric*, TimeWeightedAvgSummary>>& twa) -> void;

  static auto BuildMetricSections(
      const std::vector<std::tuple<const ITarget*, const IMetric*, MinMaxAvgSummary>>&       minmax,
      const std::vector<std::tuple<const ITarget*, const IMetric*, HistogramSummary>>&       histogram,
      const std::vector<std::tuple<const ITarget*, const IMetric*, TimeWeightedAvgSummary>>& twa)
      -> std::map<std::string, std::map<std::string, MetricTargetSummary>>;

  /**
   * @brief Write one combined stats row (Min,Max,Average,TimeWeightedAvg,Count).
   * @param csv_file    The output stream to write to
   * @param metric_name The name of the metric
   * @param target      The target this summary applies to
   * @param summary     The computed MinMaxAvg summary statistics
   * @param tw_avg      The optional time-weighted average for this (metric, target) pair
   */
  static auto WriteCombinedStatsEntry(std::ofstream& csv_file, const MetricTargetSummary& entry) -> void;

  /**
   * @brief Write the histogram table for a metric when discrete histogram summaries exist.
   *
   * @param csv_file    The output stream to write to
   * @param metric_name The metric name for lookup consistency
   * @param histogram   Discrete histogram summaries keyed by target
   */
  static auto WriteHistogramSection(std::ofstream& csv_file, const std::string& metric_name,
                                    const std::map<std::string, MetricTargetSummary>& entries_by_target) -> void;
};

}  // namespace astl

#endif  // SUMMARY_CSV_OUTPUT_HPP_
