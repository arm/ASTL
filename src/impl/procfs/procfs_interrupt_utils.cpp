// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <limits>
#include <sstream>
#include <unordered_set>

#include "astl_cxx_compat.hpp"
#include "procfs/procfs_utils.hpp"
#include "procfs/procfs_utils_readers_primitives.hpp"

namespace astl::procfs {
namespace {
auto IsCpuLabel(const std::string& cpu) -> bool {
  return cpu.starts_with("CPU") && cpu.size() > 3 &&
         std::all_of(cpu.begin() + 3, cpu.end(), [](char character) { return character >= '0' && character <= '9'; });
}

struct InterruptTable {
  std::vector<std::string>        header;
  InterruptCounterMap             counters;
  std::string                     identity;
  std::unordered_set<std::string> rows;
  std::vector<uint64_t>*          components;
  bool                            hardware;
  bool                            software;

  InterruptTable(std::vector<uint64_t>* component_counts, bool is_hardware, bool is_software)
      : components{component_counts}, hardware{is_hardware}, software{is_software} {}

  auto ReadHeader(const std::string& line) -> bool {
    header = SplitWhitespace(line);
    if (header.empty()) {
      return false;
    }
    return std::ranges::all_of(header, [&](const auto& cpu) {
      if (!IsCpuLabel(cpu) || !counters.emplace(cpu, 0).second) {
        return false;
      }
      identity += cpu + '\n';
      return true;
    });
  }

  auto IsGlobalSummary(const std::string& label) const -> bool {
    // These global hardware summaries have no per-CPU vector (Arm spells Err).
    return hardware && (label == "ERR:" || label == "Err:" || label == "MIS:");
  }

  auto ValidateRow(const std::vector<std::string>& tokens) -> bool {
    if (tokens.size() <= header.size()) {
      return false;
    }
    // Software IRQ rows have no controller/description suffix.
    if (software && tokens.size() != header.size() + 1) {
      return false;
    }
    const auto& label = tokens.front();
    return label.size() >= 2 && label.ends_with(':') && rows.insert(label).second;
  }

  auto AddColumns(const std::vector<std::string>& tokens) -> bool {
    for (size_t index = 0; index < header.size(); ++index) {
      const auto value = detail::ParseUint64Token(tokens[index + 1]);
      auto&      total = counters.at(header[index]);
      if (!value || *value > std::numeric_limits<uint64_t>::max() - total) {
        return false;
      }
      total += *value;
      if (components) {
        components->push_back(*value);
      }
    }
    return true;
  }

  auto AddRow(const std::string& line) -> bool {
    const auto tokens = SplitWhitespace(line);
    if (tokens.empty() || IsGlobalSummary(tokens.front())) {
      return true;
    }
    if (!ValidateRow(tokens) || !AddColumns(tokens)) {
      return false;
    }
    identity += tokens.front() + '\n';
    for (size_t index = header.size() + 1; index < tokens.size(); ++index) {
      identity += tokens[index] + '\n';
    }
    return true;
  }
};
}  // namespace

auto ReadInterruptCounters(const FileInterface& files, const std::filesystem::path& path, std::string* source_identity,
                           std::vector<uint64_t>* component_counts)
    -> astl::compat::expected<InterruptCounterMap, astl_status_code> {
  std::string contents;
  const auto  status = files.Read(path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    return astl::compat::unexpected(status);
  }
  std::istringstream stream{contents};
  std::string        line;
  InterruptTable     table{component_counts, path == "interrupts", path == "softirqs"};
  if (!std::getline(stream, line) || !table.ReadHeader(line)) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (component_counts) {
    component_counts->clear();
  }
  while (std::getline(stream, line)) {
    if (!table.AddRow(line)) {
      return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
  }
  if (table.rows.empty()) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (source_identity) {
    *source_identity = std::move(table.identity);
  }
  return std::move(table.counters);
}
}  // namespace astl::procfs
