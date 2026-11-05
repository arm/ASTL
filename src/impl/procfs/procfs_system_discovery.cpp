// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <sstream>

#include "procfs/procfs_system_utils.hpp"

namespace astl::procfs {
namespace {
class SystemCounterDiscovery {
 public:
  explicit SystemCounterDiscovery(const FileInterface& source_files) : _files{&source_files} {}

  auto Discover() -> std::vector<MetricDescriptor> {
    for (const auto& path : {"diskstats", "net/dev", "vmstat", "interrupts", "softirqs"}) {
      AppendFileCounters(path);
    }
    std::error_code error;
    if (std::filesystem::is_directory(_files->GetBasePath() / ".", error)) {
      for (const auto& kind : {"processes", "threads"}) {
        AppendCounter(
            "system.current_" + std::string{kind},
            "Instantaneous visible " + std::string{kind} + "; procfs namespace/permission scope; non-atomic scan",
            SystemField{".", "", kind}, ASTL_UNITS_COUNT);
      }
    }
    return std::move(_descriptors);
  }

 private:
  auto AppendCounter(std::string name, std::string description, const SystemField& field, astl_units_t units) -> void {
    _descriptors.push_back(MetricDescriptor{.metric_name      = name,
                                            .metric_id_suffix = name,
                                            .description      = description,
                                            .units            = units,
                                            .value_type       = ASTL_VALUE_UINT64,
                                            .input_value_type = ASTL_VALUE_UINT64,
                                            .identifier       = ASTL_METRIC_IDENTIFIER_COUNT,
                                            .field_descriptor = field});
  }

  auto AppendReadableCounter(std::string name, std::string description, const SystemField& field, astl_units_t units)
      -> void {
    if (ReadSystemField(*_files, field)) {
      AppendCounter(std::move(name), std::move(description), field, units);
    }
  }

  auto AppendDiskCounters(const std::vector<std::string>& tokens) -> void {
    if (tokens.size() < detail::kDiskStatsMinTokenCount) {
      return;
    }
    for (const auto& kind : {"read_operations", "write_operations", "read_bytes", "write_bytes"}) {
      AppendReadableCounter("diskstats." + tokens[2] + "." + kind,
                            "Cumulative " + std::string{kind} + " for " + tokens[2] + " (major:minor " + tokens[0] +
                                ":" + tokens[1] + "); no automatic aggregation of disks, partitions or storage layers",
                            SystemField{"diskstats", tokens[2], kind},
                            std::string_view{kind}.ends_with("bytes") ? ASTL_UNITS_BYTES : ASTL_UNITS_COUNT);
    }
  }

  auto AppendNetworkCounters(const std::string& line) -> void {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      return;
    }
    const std::string label{Trim(std::string_view{line}.substr(0, colon))};
    for (const auto& kind : {"rx_bytes", "tx_bytes"}) {
      AppendReadableCounter("netdev." + label + "." + kind,
                            "Cumulative " + std::string{kind} + " on " + label + "; no automatic interface aggregation",
                            SystemField{"net/dev", label, kind}, ASTL_UNITS_BYTES);
    }
  }

  auto AppendVmCounter(const std::vector<std::string>& tokens) -> void {
    if (tokens.size() == 2 && (tokens[0] == "pgfault" || tokens[0] == "pgmajfault")) {
      AppendReadableCounter("vmstat." + tokens[0], "Cumulative system page faults since boot",
                            SystemField{"vmstat", "", tokens[0]}, ASTL_UNITS_COUNT);
    }
  }

  auto AppendInterruptCounters(const std::string& path) -> void {
    const auto counters = ReadInterruptCounters(*_files, path);
    if (!counters) {
      return;
    }
    for (const auto& [cpu, value] : *counters) {
      (void)value;
      AppendCounter(path + "." + cpu, "Cumulative per-CPU " + path + " column sum",
                    SystemField{path, cpu, "interrupts"}, ASTL_UNITS_COUNT);
    }
  }

  auto AppendFileCounters(const std::string& path) -> void {
    std::string contents;
    if (_files->Read(path, contents) != ASTL_STATUS_SUCCESS) {
      return;
    }
    if (path == "interrupts" || path == "softirqs") {
      AppendInterruptCounters(path);
      return;
    }
    std::istringstream stream{contents};
    std::string        line;
    while (std::getline(stream, line)) {
      if (path == "diskstats") {
        AppendDiskCounters(SplitWhitespace(line));
      } else if (path == "net/dev") {
        AppendNetworkCounters(line);
      } else if (path == "vmstat") {
        AppendVmCounter(SplitWhitespace(line));
      }
    }
  }

  const FileInterface*          _files;
  std::vector<MetricDescriptor> _descriptors;
};
}  // namespace

auto DiscoverSystemCounterDescriptors(const FileInterface& files) -> std::vector<MetricDescriptor> {
  return SystemCounterDiscovery{files}.Discover();
}
}  // namespace astl::procfs
