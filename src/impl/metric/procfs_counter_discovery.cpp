// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "astl_logger.hpp"
#include "common/key_value_text_utils.hpp"
#include "common/procfs_utils.hpp"

namespace astl::procfs {

namespace {

constexpr size_t   kCpuFieldNameCount         = 10;
constexpr uint64_t kBytesPerKilobyte          = 1024;
constexpr size_t   kLoadavgRequiredTokenCount = 5;

using CpuFieldNames = std::array<std::string_view, kCpuFieldNameCount>;

auto TryReadFile(const FileInterface& file_interface, const std::filesystem::path& relative_path)
    -> std::optional<std::string> {
  std::string contents;
  const auto  status = file_interface.Read(relative_path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_DEBUG("procfs TryReadFile: unable to read {}", relative_path.string());
    return std::nullopt;
  }
  return contents;
}

auto ResolveCpuFieldName(size_t field_index, const CpuFieldNames& cpu_field_names) -> std::string {
  return field_index < cpu_field_names.size() ? std::string{cpu_field_names.at(field_index)}
                                              : "field" + std::to_string(field_index);
}

void AppendCpuStatCounterDescriptors(std::vector<MetricDescriptor>& descriptors, std::string_view label,
                                     const std::vector<std::string>& tokens, const CpuFieldNames& cpu_field_names) {
  const std::string label_string{label};
  for (size_t token_index = 1; token_index < tokens.size(); ++token_index) {
    const auto field_index = token_index - 1;
    const auto field_name  = ResolveCpuFieldName(field_index, cpu_field_names);
    descriptors.push_back(MetricDescriptor{
        .metric_name      = "stat." + label_string + "." + field_name,
        .metric_id_suffix = "stat::" + label_string + "::" + field_name,
        .description      = "Procfs /proc/stat " + label_string + " field " + field_name,
        .units            = ASTL_UNITS_TICKS,
        .value_type       = ASTL_VALUE_UINT64,
        .input_value_type = ASTL_VALUE_UINT64,
        .identifier       = ASTL_METRIC_IDENTIFIER_COUNT,
        .field_descriptor = TokenField{"stat", label_string, token_index, ASTL_VALUE_UINT64},
    });
  }
}

auto BuildScalarStatCounterDescriptor(std::string_view                                     label,
                                      const std::unordered_map<std::string, astl_units_t>& scalar_fields,
                                      const std::vector<std::string>& tokens) -> std::optional<MetricDescriptor> {
  if (tokens.size() <= 1) {
    return std::nullopt;
  }

  const auto label_string = std::string{label};
  const auto it           = scalar_fields.find(label_string);
  if (it == scalar_fields.end()) {
    return std::nullopt;
  }

  return MetricDescriptor{
      .metric_name      = "stat." + label_string,
      .metric_id_suffix = "stat::" + label_string,
      .description      = "Procfs /proc/stat field " + label_string,
      .units            = it->second,
      .value_type       = ASTL_VALUE_UINT64,
      .input_value_type = ASTL_VALUE_UINT64,
      .identifier       = label == "btime" ? ASTL_METRIC_IDENTIFIER_UNKNOWN : ASTL_METRIC_IDENTIFIER_COUNT,
      .field_descriptor = TokenField{"stat", label_string, 1, ASTL_VALUE_UINT64},
  };
}

void AppendStatCounterDescriptorsForLine(std::vector<MetricDescriptor>&  descriptors,
                                         const std::vector<std::string>& tokens, const CpuFieldNames& cpu_field_names,
                                         const std::unordered_map<std::string, astl_units_t>& scalar_fields) {
  if (tokens.empty()) {
    return;
  }

  const std::string_view label = tokens.front();
  if (label.rfind("cpu", 0) == 0 && tokens.size() > 1) {
    AppendCpuStatCounterDescriptors(descriptors, label, tokens, cpu_field_names);
    return;
  }

  if (auto descriptor = BuildScalarStatCounterDescriptor(label, scalar_fields, tokens); descriptor.has_value()) {
    descriptors.push_back(std::move(*descriptor));
  }
}

auto DiscoverMeminfoCounterDescriptors(const FileInterface& file_interface) -> std::vector<MetricDescriptor> {
  auto contents = TryReadFile(file_interface, "meminfo");
  if (!contents.has_value()) {
    return {};
  }

  std::vector<MetricDescriptor> descriptors;
  size_t                        position = 0;
  while (position <= contents->size()) {
    const size_t end = contents->find('\n', position);
    const auto   line =
        std::string_view{*contents}.substr(position, end == std::string::npos ? std::string::npos : end - position);
    if (const auto parsed = text::ParseKeyValueLine(line); parsed.has_value()) {
      const std::vector<std::string> tokens = SplitWhitespace(parsed->value);
      if (!tokens.empty()) {
        MetricDescriptor descriptor{
            .metric_name      = "meminfo." + std::string{parsed->key},
            .metric_id_suffix = "meminfo::" + std::string{parsed->key},
            .description      = "Procfs /proc/meminfo field " + std::string{parsed->key},
            .units            = ASTL_UNITS_NONE,
            .value_type       = ASTL_VALUE_UINT64,
            .input_value_type = ASTL_VALUE_UINT64,
            .identifier       = ASTL_METRIC_IDENTIFIER_COUNT,
            .field_descriptor = KeyValueField{"meminfo", std::string{parsed->key}, ASTL_VALUE_UINT64},
        };

        if (tokens.size() > 1 && tokens[1] == "kB") {
          descriptor.units             = ASTL_UNITS_BYTES;
          descriptor.value_type        = ASTL_VALUE_FLOAT64;
          descriptor.scale_numerator   = kBytesPerKilobyte;
          descriptor.scale_denominator = 1;
          descriptor.identifier        = ASTL_METRIC_IDENTIFIER_UNKNOWN;
        }
        descriptors.push_back(std::move(descriptor));
      }
    }
    if (end == std::string::npos) {
      break;
    }
    position = end + 1;
  }
  return descriptors;
}

auto DiscoverStatCounterDescriptors(const FileInterface& file_interface) -> std::vector<MetricDescriptor> {
  auto contents = TryReadFile(file_interface, "stat");
  if (!contents.has_value()) {
    return {};
  }

  constexpr CpuFieldNames                             cpu_field_names = {"user", "nice",    "system", "idle",  "iowait",
                                                                         "irq",  "softirq", "steal",  "guest", "guest_nice"};
  const std::unordered_map<std::string, astl_units_t> scalar_fields   = {
      {"intr",          ASTL_UNITS_NONE   },
      {"ctxt",          ASTL_UNITS_NONE   },
      {"softirq",       ASTL_UNITS_NONE   },
      {"processes",     ASTL_UNITS_NONE   },
      {"procs_running", ASTL_UNITS_NONE   },
      {"procs_blocked", ASTL_UNITS_NONE   },
      {"btime",         ASTL_UNITS_SECONDS},
  };

  std::vector<MetricDescriptor> descriptors;
  size_t                        position = 0;
  while (position <= contents->size()) {
    const size_t end = contents->find('\n', position);
    const auto   line =
        std::string_view{*contents}.substr(position, end == std::string::npos ? std::string::npos : end - position);
    auto tokens = SplitWhitespace(line);
    AppendStatCounterDescriptorsForLine(descriptors, tokens, cpu_field_names, scalar_fields);
    if (end == std::string::npos) {
      break;
    }
    position = end + 1;
  }
  return descriptors;
}

auto DiscoverLoadavgCounterDescriptors(const FileInterface& file_interface) -> std::vector<MetricDescriptor> {
  auto contents = TryReadFile(file_interface, "loadavg");
  if (!contents.has_value()) {
    return {};
  }

  const auto tokens = SplitWhitespace(*contents);
  if (tokens.size() < kLoadavgRequiredTokenCount) {
    ASTL_LOG_WARNING("procfs DiscoverLoadavgCounterDescriptors: unexpected /proc/loadavg format");
    return {};
  }

  return {
      MetricDescriptor{.metric_name      = "loadavg.1m",
                       .metric_id_suffix = "loadavg::1m",
                       .description      = "Procfs /proc/loadavg 1 minute average",
                       .units            = ASTL_UNITS_NONE,
                       .value_type       = ASTL_VALUE_FLOAT64,
                       .input_value_type = ASTL_VALUE_FLOAT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                       .field_descriptor = TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}                           },
      MetricDescriptor{.metric_name      = "loadavg.5m",
                       .metric_id_suffix = "loadavg::5m",
                       .description      = "Procfs /proc/loadavg 5 minute average",
                       .units            = ASTL_UNITS_NONE,
                       .value_type       = ASTL_VALUE_FLOAT64,
                       .input_value_type = ASTL_VALUE_FLOAT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                       .field_descriptor = TokenField{"loadavg", "", 1, ASTL_VALUE_FLOAT64}                           },
      MetricDescriptor{.metric_name      = "loadavg.15m",
                       .metric_id_suffix = "loadavg::15m",
                       .description      = "Procfs /proc/loadavg 15 minute average",
                       .units            = ASTL_UNITS_NONE,
                       .value_type       = ASTL_VALUE_FLOAT64,
                       .input_value_type = ASTL_VALUE_FLOAT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                       .field_descriptor = TokenField{"loadavg", "", 2, ASTL_VALUE_FLOAT64}                           },
      MetricDescriptor{.metric_name      = "loadavg.runnable_tasks",
                       .metric_id_suffix = "loadavg::runnable_tasks",
                       .description      = "Procfs /proc/loadavg runnable tasks",
                       .units            = ASTL_UNITS_NONE,
                       .value_type       = ASTL_VALUE_UINT64,
                       .input_value_type = ASTL_VALUE_UINT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_COUNT,
                       .field_descriptor =
                           SplitTokenField{"loadavg", "", 3, '/', SplitTokenPart::BEFORE_DELIMITER, ASTL_VALUE_UINT64}},
      MetricDescriptor{.metric_name      = "loadavg.total_tasks",
                       .metric_id_suffix = "loadavg::total_tasks",
                       .description      = "Procfs /proc/loadavg total tasks",
                       .units            = ASTL_UNITS_NONE,
                       .value_type       = ASTL_VALUE_UINT64,
                       .input_value_type = ASTL_VALUE_UINT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_COUNT,
                       .field_descriptor =
                           SplitTokenField{"loadavg", "", 3, '/', SplitTokenPart::AFTER_DELIMITER, ASTL_VALUE_UINT64} },
      MetricDescriptor{.metric_name      = "loadavg.last_pid",
                       .metric_id_suffix = "loadavg::last_pid",
                       .description      = "Procfs /proc/loadavg last created pid",
                       .units            = ASTL_UNITS_NONE,
                       .value_type       = ASTL_VALUE_UINT64,
                       .input_value_type = ASTL_VALUE_UINT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_COUNT,
                       .field_descriptor = TokenField{"loadavg", "", 4, ASTL_VALUE_UINT64}                            },
  };
}

auto DiscoverUptimeCounterDescriptors(const FileInterface& file_interface) -> std::vector<MetricDescriptor> {
  auto contents = TryReadFile(file_interface, "uptime");
  if (!contents.has_value()) {
    return {};
  }

  const auto tokens = SplitWhitespace(*contents);
  if (tokens.size() < 2) {
    ASTL_LOG_WARNING("procfs DiscoverUptimeCounterDescriptors: unexpected /proc/uptime format");
    return {};
  }

  return {
      MetricDescriptor{.metric_name      = "uptime.total",
                       .metric_id_suffix = "uptime::total",
                       .description      = "Procfs /proc/uptime total uptime seconds",
                       .units            = ASTL_UNITS_SECONDS,
                       .value_type       = ASTL_VALUE_FLOAT64,
                       .input_value_type = ASTL_VALUE_FLOAT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                       .field_descriptor = TokenField{"uptime", "", 0, ASTL_VALUE_FLOAT64}},
      MetricDescriptor{.metric_name      = "uptime.idle",
                       .metric_id_suffix = "uptime::idle",
                       .description      = "Procfs /proc/uptime aggregate idle seconds",
                       .units            = ASTL_UNITS_SECONDS,
                       .value_type       = ASTL_VALUE_FLOAT64,
                       .input_value_type = ASTL_VALUE_FLOAT64,
                       .identifier       = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                       .field_descriptor = TokenField{"uptime", "", 1, ASTL_VALUE_FLOAT64}},
  };
}

}  // namespace

auto DiscoverCounterDescriptors(const FileInterface& file_interface)
    -> std::expected<std::vector<MetricDescriptor>, astl_status_code> {
  std::vector<MetricDescriptor> descriptors;

  auto append_descriptors = [&descriptors](std::vector<MetricDescriptor> source) -> void {
    descriptors.reserve(descriptors.size() + source.size());
    std::move(source.begin(), source.end(), std::back_inserter(descriptors));
  };

  append_descriptors(DiscoverMeminfoCounterDescriptors(file_interface));
  append_descriptors(DiscoverStatCounterDescriptors(file_interface));
  append_descriptors(DiscoverLoadavgCounterDescriptors(file_interface));
  append_descriptors(DiscoverUptimeCounterDescriptors(file_interface));

  return descriptors;
}

}  // namespace astl::procfs
