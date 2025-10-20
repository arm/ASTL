#include "perfetto_output.hpp"

#include <cstdint>
#include <sstream>
#include <utility>

#include "astl/astl.h"
#include "common/astl_value.hpp"

namespace astl {

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

// Basic JSON escaping for strings used in instant events (escape backslash and quote).
auto PerfettoOutput::EscapeJsonString(std::string_view input) -> std::string {
  std::string escaped;
  escaped.reserve(input.size());
  for (char character : input) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
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
      std::string              metric_name = Sanitize(metric->Name());
      int                      tid         = GetTid(target, metric);
      astl_metric_properties_t props{};  // NOLINT(cppcoreguidelines-pro-type-member-init)
      metric->GetProperties(&props);

      for (const auto& sample : metric_samples) {
        std::visit(
            [&](const auto& inner_value) {
              using ValueT = std::decay_t<decltype(inner_value)>;
              if (!_first_event) {
                _trace_stream << ",\n";
              }
              _first_event = false;

              uint64_t    ts_us            = static_cast<uint64_t>(sample.timestamp.time_since_epoch().count());
              bool        is_string_sample = !std::is_arithmetic_v<ValueT>;
              std::string category         = PerfettoOutput::DetermineCategory(props._units, is_string_sample);
              std::string composite_name   = target_name + "." + metric_name;
              if constexpr (std::is_arithmetic_v<ValueT>) {
                _trace_stream << R"({"ph":"C","cat":")" << category << R"(","name":")" << composite_name << R"(","ts":)"
                              << ts_us << R"(,"pid":)" << pid << R"(,"tid":)" << tid << R"(,"args":{"target":")"
                              << target_name << R"(","metric":")" << metric_name << R"(","value":)" << inner_value
                              << "}}";
              } else {
                std::string value_str = EscapeJsonString(static_cast<std::string>(inner_value));
                _trace_stream << R"({"ph":"I","cat":")" << category << R"(","name":")" << composite_name << R"(","ts":)"
                              << ts_us << R"(,"pid":)" << pid << R"(,"tid":)" << tid << R"(,"s":"t","args":{"target":")"
                              << target_name << R"(","metric":")" << metric_name << R"(","value":")" << value_str
                              << R"("}})";
              }
            },
            sample.value.value);
      }
    }
  }
  _trace_stream.flush();
  return ASTL_STATUS_SUCCESS;
}

auto PerfettoOutput::DetermineCategory(astl_units_t units, bool is_string_sample) -> std::string {
  switch (units) {
    case ASTL_UNITS_WATTS:
      return "Power";
    case ASTL_UNITS_JOULES:
      return "Energy";
    case ASTL_UNITS_CELSIUS:
      return "Temperature";
    case ASTL_UNITS_MHERTZ:
      return "Frequency";
    case ASTL_UNITS_VOLTS:
      return "Voltage";
    case ASTL_UNITS_AMPS:
      return "Current";
    case ASTL_UNITS_BYTES:
      return "Bytes";
    case ASTL_UNITS_MBYTESPERSEC:
      return "Bandwidth";
    case ASTL_UNITS_TICKS:
      return "Ticks";
    case ASTL_UNITS_SECONDS:
      return "Time";
    default:
      break;
  }
  if (is_string_sample) {
    return "State";  // categorize string (event/state) metrics without quantitative unit
  }
  return "";  // fallback empty category
}

}  // namespace astl