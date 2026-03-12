// SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#include "interval_csv_output.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
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
// Forward declaration so the template visitor can find it during instantiation
inline void SanitizeDescription(std::string& description);

/** @brief Helper to emit a value variant to a stream. */
inline void EmitValue(std::ostream& output_stream, const AstlValue& value) {
  std::visit([&](const auto& inner_value) { output_stream << inner_value; }, value.value);
}

}  // namespace

namespace {
/** @brief Row wrapper pairing a target and a processed sample. */
struct SampleRow {
  const ITarget*              target{nullptr};
  const ProcessedSampledData* sample{nullptr};
};
/** @brief Aggregated metric group: description + collected sample rows. */
struct MetricGroup {
  std::string            description;
  std::vector<SampleRow> rows;
  MetricGroup() = default;
};

/** @brief Replace double quotes in description with single quotes for CSV safety. */
inline void SanitizeDescription(std::string& description) {
  std::replace(description.begin(), description.end(), '"', '\'');
}

// Build groups map aggregating by metric name
/** @brief Aggregate samples by metric name; first non-empty description captured.
 *  Skips null targets/metrics and empty sample vectors.
 */
inline std::unordered_map<std::string, MetricGroup> BuildGroups(const ProcessedSamplesMap& processed) {
  std::unordered_map<std::string, MetricGroup> groups;
  groups.reserve(processed.size());
  for (const auto& [target_ptr, metric_map] : processed) {
    if (!target_ptr) {
      ASTL_LOG_ERROR("IntervalCsvOutput: processed samples contain a null target pointer (skipping)");
      continue;  // Corrupted entry; skip instead of aborting entire export.
    }
    for (const auto& [metric_ptr, samples] : metric_map) {
      if (!metric_ptr || samples.empty()) {
        continue;
      }
      const std::string& metric_name = metric_ptr->Name();
      auto&              group       = groups[metric_name];
      if (group.description.empty()) {
        astl_metric_props_t props{};
        metric_ptr->GetProperties(&props);
        if (props.description && *props.description) {
          group.description = props.description;
        }
      }
      group.rows.reserve(group.rows.size() + samples.size());
      for (const auto& sample : samples) {
        group.rows.emplace_back(SampleRow{target_ptr, &sample});
      }
    }
  }
  return groups;
}

// Emit a single metric group (hybrid format: info row + header including metric name + sample rows)
/** @brief Emit one metric group (info row, header, sample rows, blank separator). */
void EmitGroup(std::ostream& output_stream, const std::string& metric_name, const MetricGroup& group) {
  std::string description  = group.description;
  bool        needs_quotes = description.find(',') != std::string::npos || description.find('"') != std::string::npos;

  if (needs_quotes) {
    SanitizeDescription(description);
    output_stream << metric_name << ',' << '"' << description << '"' << '\n';
  } else {
    output_stream << metric_name << ',' << description << '\n';
  }
  // Per-metric header now includes metric column
  output_stream << "timestamp_us,target,metric,value\n";
  for (const auto& row : group.rows) {
    if (!row.target || !row.sample) {
      continue;
    }
    const uint64_t ts_us = static_cast<uint64_t>(row.sample->timestamp.time_since_epoch().count());
    output_stream << ts_us << ',' << row.target->Name() << ',' << metric_name << ',';
    EmitValue(output_stream, row.sample->value);
    output_stream << '\n';
  }
  output_stream << '\n';
}
}  // namespace

auto IntervalCsvOutput::WriteProcessedSamples(const ProcessedSamplesMap& processed) -> astl_status_code {
  if (!Ready()) {
    return ASTL_STATUS_INTERNAL_ERROR;  // stream not open
  }

  WriteSystemInfoCsvSection(_output_stream);

  auto groups = BuildGroups(processed);

  if (groups.empty()) {
    _output_stream.flush();
    return ASTL_STATUS_SUCCESS;  // nothing to write
  }

  std::vector<std::string> ordered_metric_names;
  ordered_metric_names.reserve(groups.size());
  for (const auto& entry : groups) {
    ordered_metric_names.push_back(entry.first);
  }
  std::sort(ordered_metric_names.begin(), ordered_metric_names.end());

  for (const auto& name : ordered_metric_names) {
    EmitGroup(_output_stream, name, groups[name]);
  }

  _output_stream.close();
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
