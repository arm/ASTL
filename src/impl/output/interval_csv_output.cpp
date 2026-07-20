// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#include "interval_csv_output.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "astl/astl_errors.h"
#include "csv_system_info.hpp"
#include "metric/i_metric.hpp"
#include "target.hpp"

namespace astl {

IntervalCsvOutput::IntervalCsvOutput(std::filesystem::path path) : _path(std::move(path)) {
  std::error_code err_code;
  std::filesystem::create_directories(_path.parent_path(), err_code);  // best-effort
  _output_stream.open(_path, std::ios::out | std::ios::trunc);
  if (!_output_stream.is_open()) {
    ASTL_LOG_ERROR("IntervalCsvOutput: failed to open path '{}'", _path.string());
  }
}

namespace {
struct ReportSection {
  std::string                              metric_name;
  std::string                              target_name;
  std::vector<const ProcessedSampledData*> samples;
};

inline auto BuildSections(const ProcessedSamplesMap& processed) -> std::vector<ReportSection> {
  std::vector<ReportSection> sections;
  for (const auto& [target_ptr, metric_map] : processed) {
    if (!target_ptr) {
      ASTL_LOG_ERROR("IntervalCsvOutput: processed samples contain a null target pointer (skipping)");
      continue;
    }
    for (const auto& [metric_ptr, samples] : metric_map) {
      if (!metric_ptr || samples.empty()) {
        continue;
      }
      ReportSection section{
          .metric_name = metric_ptr->Name(),
          .target_name = target_ptr->Name(),
          .samples     = {},
      };
      section.samples.reserve(samples.size());
      for (const auto& sample : samples) {
        section.samples.push_back(&sample);
      }
      std::ranges::sort(section.samples,
                        [](const auto* left, const auto* right) { return left->timestamp < right->timestamp; });
      sections.push_back(std::move(section));
    }
  }

  std::ranges::sort(sections, [](const auto& left, const auto& right) {
    return std::tie(left.metric_name, left.target_name) < std::tie(right.metric_name, right.target_name);
  });
  return sections;
}

void EmitSection(std::ostream& output_stream, const ReportSection& section) {
  output_stream << section.metric_name << " on " << section.target_name << "\n\n";
  output_stream << "timestamp_us,value\n";
  for (const auto* sample : section.samples) {
    if (!sample) {
      continue;
    }
    const uint64_t ts_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(sample->timestamp.time_since_epoch()).count());
    output_stream << ts_us << ',' << FormatReportValue(sample->value) << '\n';
  }
  output_stream << '\n';
}
}  // namespace

auto IntervalCsvOutput::WriteProcessedSamples(const ProcessedSamplesMap& processed) -> astl_status_code {
  if (!Ready()) {
    return ASTL_STATUS_INTERNAL_ERROR;  // stream not open
  }

  WriteCollectionInfoCsvSection(_output_stream);

  auto sections = BuildSections(processed);
  if (sections.empty()) {
    _output_stream.flush();
    return ASTL_STATUS_SUCCESS;
  }

  for (const auto& section : sections) {
    EmitSection(_output_stream, section);
  }

  _output_stream.close();
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
