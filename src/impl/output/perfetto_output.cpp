// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "perfetto_output.hpp"

#include <cstdint>
#include <utility>

#include "common/astl_value.hpp"
#include "common/system_info.hpp"

namespace astl {

namespace {

auto EscapeJsonString(std::string_view input) -> std::string {
  std::string escaped;
  escaped.reserve(input.size());
  for (const char character : input) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

}  // namespace

PerfettoOutput::PerfettoOutput(std::filesystem::path path) : _path(std::move(path)) {
  // Open (truncate) the output file early. If this fails Ready() will remain false and writes are skipped.
  _trace_stream.open(_path, std::ios::out | std::ios::trunc);
  if (_trace_stream.is_open()) {
    WriteHeader();
    ASTL_LOG_INFO("PerfettoOutput: JSON trace -> '{}'", _path.string());
  } else {
    ASTL_LOG_ERROR("PerfettoOutput: cannot open '{}'", _path.string());
  }
}

PerfettoOutput::~PerfettoOutput() {
  if (_trace_stream.is_open() && _opened_json) {
    WriteFooter();
  }
}

auto PerfettoOutput::Sanitize(std::string_view input) -> std::string {
  std::string sanitized(input);
  for (auto& character : sanitized) {
    if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
      character = '_';
    }
    if (character == '"') {
      character = '_';
    }
  }
  return sanitized;
}

auto PerfettoOutput::GetPid(const ITarget* target) -> int {
  if (target == nullptr) {
    return 0;
  }
  auto it = _pid_map.find(target);
  if (it != _pid_map.end()) {
    return it->second;
  }
  int assigned = _next_pid++;
  _pid_map.emplace(target, assigned);
  // Initialize tid counter for this target.
  _next_tid_map.emplace(target, 1);
  // Emit process metadata event naming this target to label the track.
  if (_trace_stream.is_open()) {
    std::string target_name = Sanitize(target->Name());
    if (!_first_event) {
      _trace_stream << ",\n";
    }
    _first_event = false;
    _trace_stream << R"({"ph":"M","pid":)" << assigned << R"(,"name":"process_name","args":{"name":")" << target_name
                  << R"("}})";
  }
  return assigned;
}

auto PerfettoOutput::GetTid(const ITarget* target, const IMetric* metric) -> int {
  if (target == nullptr || metric == nullptr) {
    return 0;
  }
  auto& metric_tid_map = _tid_map[target];
  auto  it             = metric_tid_map.find(metric);
  if (it != metric_tid_map.end()) {
    return it->second;
  }
  // Assign next tid for this target.
  int next_tid = _next_tid_map[target]++;
  metric_tid_map.emplace(metric, next_tid);
  // Emit thread metadata event naming this metric under its target.
  if (_trace_stream.is_open()) {
    std::string metric_name = Sanitize(metric->Name());
    if (!_first_event) {
      _trace_stream << ",\n";
    }
    _first_event = false;
    _trace_stream << R"({"ph":"M","pid":)" << _pid_map[target] << R"(,"tid":)" << next_tid
                  << R"(,"name":"thread_name","args":{"name":")" << metric_name << R"("}})";
  }
  return next_tid;
}

auto PerfettoOutput::WriteHeader() -> void {
  if (_opened_json) {
    return;
  }
  _trace_stream << "[\n";
  // Emit trace-level metadata declaring timestamps are microseconds for Perfetto UI.
  // Use pid 0 (convention for global metadata). This does not allocate target pids/tids.
  _trace_stream << R"({"ph":"M","pid":0,"name":"trace_metadata","args":{"displayTimeUnit":"us"}})";

  const auto& info = GetActivePlatformInfo();
  _trace_stream << ",\n";
  _trace_stream << R"({"ph":"M","pid":0,"name":"astl_system_info","args":{"soc_name":")"
                << EscapeJsonString(info.soc_name) << R"(","vendor_id":")" << EscapeJsonString(info.vendor_id)
                << R"(","os_name":")" << EscapeJsonString(info.os_name) << R"(","kernel_name":")"
                << EscapeJsonString(info.kernel_name) << R"(","kernel_release":")"
                << EscapeJsonString(info.kernel_release) << R"(","kernel_version":")"
                << EscapeJsonString(info.kernel_version) << R"(","firmware_version":")"
                << EscapeJsonString(info.firmware_version) << R"(","hostname":")" << EscapeJsonString(info.hostname)
                << R"(","architecture":")" << EscapeJsonString(info.architecture) << R"("}})";

  _first_event = false;  // We have written the first element; subsequent events need leading comma.
  _opened_json = true;
}

auto PerfettoOutput::WriteFooter() -> void { _trace_stream << "\n]\n"; }

auto PerfettoOutput::WriteProcessedSamples(const ProcessedSamplesMap& samples) -> astl_status_code {
  if (!Ready()) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  for (const auto& target_entry : samples) {
    const ITarget* target = target_entry.first;
    if (!target) {
      continue;
    }
    std::string target_name = Sanitize(target->Name());
    int         pid         = GetPid(target);

    for (const auto& metric_entry : target_entry.second) {
      const IMetric* metric         = metric_entry.first;
      const auto&    metric_samples = metric_entry.second;
      if (!metric || metric_samples.empty()) {
        continue;
      }
      std::string         metric_name = Sanitize(metric->Name());
      int                 tid         = GetTid(target, metric);
      astl_metric_props_t props{};  // NOLINT(cppcoreguidelines-pro-type-member-init)
      metric->GetProperties(&props);

      for (const auto& sample : metric_samples) {
        std::visit(
            [&](const auto& inner_value) {
              if (!_first_event) {
                _trace_stream << ",\n";
              }
              _first_event = false;

              uint64_t    ts_us          = static_cast<uint64_t>(sample.timestamp.time_since_epoch().count());
              std::string category       = PerfettoOutput::DetermineCategory(props.units);
              std::string composite_name = target_name + "." + metric_name;
              _trace_stream << R"({"ph":"C","cat":")" << category << R"(","name":")" << composite_name << R"(","ts":)"
                            << ts_us << R"(,"pid":)" << pid << R"(,"tid":)" << tid << R"(,"args":{"target":")"
                            << target_name << R"(","metric":")" << metric_name << R"(","value":)" << inner_value
                            << "}}";
            },
            sample.value.value);
      }
    }
  }
  _trace_stream.flush();
  return ASTL_STATUS_SUCCESS;
}

auto PerfettoOutput::DetermineCategory(astl_units_t units) -> std::string {
  std::string category;
  switch (units) {
    case ASTL_UNITS_JOULES:
      category = "Energy";
      break;
    case ASTL_UNITS_WATTS:
      category = "Power";
      break;
    case ASTL_UNITS_CELSIUS:
      category = "Temperature";
      break;
    case ASTL_UNITS_MHERTZ:
      category = "Frequency";
      break;
    case ASTL_UNITS_VOLTS:
      category = "Voltage";
      break;
    case ASTL_UNITS_AMPS:
      category = "Current";
      break;
    case ASTL_UNITS_BYTES:
      category = "Bytes";
      break;
    case ASTL_UNITS_MBYTESPERSEC:
      category = "Bandwidth";
      break;
    case ASTL_UNITS_TICKS:
      category = "Ticks";
      break;
    case ASTL_UNITS_SECONDS:
      category = "Time";
      break;
    case ASTL_UNITS_PERCENT:
      category = "Percent";
      break;
    default:
      break;
  }
  return category;
}

}  // namespace astl
