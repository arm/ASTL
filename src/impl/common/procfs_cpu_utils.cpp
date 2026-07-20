// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cctype>
#include <string_view>

#include "common/procfs_utils_readers.hpp"
#include "common/procfs_utils_readers_primitives.hpp"

namespace astl::procfs::detail {

namespace {

// /proc/stat CPU lines use the following token layout:
//   0: CPU label, 1: user, 2: nice, 3: system, 4: idle,
//   5: iowait, 6: irq, 7: softirq, 8: steal,
//   9: guest, 10: guest_nice.
//
// A valid line must contain fields through idle. Total CPU time includes fields 1-8 only; guest and guest_nice are
// excluded because Linux already includes them in user and nice.
constexpr size_t kCpuSnapshotRequiredTokenCount = 5;
constexpr size_t kCpuIdleTokenIndex             = 4;
constexpr size_t kCpuIoWaitTokenIndex           = 5;
constexpr size_t kCpuLastTotalTokenIndex        = 8;

auto IsCpuLinePrefix(std::string_view token) -> bool {
  if (token == "cpu") {
    return true;
  }
  constexpr std::string_view k_cpu_prefix{"cpu"};
  if (!token.starts_with(k_cpu_prefix)) {
    return false;
  }
  const auto cpu_index = token.substr(k_cpu_prefix.size());
  return !cpu_index.empty() && std::all_of(cpu_index.begin(), cpu_index.end(), [](char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
  });
}

auto ParseCpuSnapshotTokens(const std::vector<std::string>& tokens) -> std::expected<CpuSnapshot, astl_status_code> {
  if (tokens.size() < kCpuSnapshotRequiredTokenCount) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  uint64_t     total_ticks{0};
  uint64_t     idle_ticks{0};
  const size_t last_token_index = std::min(tokens.size() - 1, kCpuLastTotalTokenIndex);
  for (size_t token_index = 1; token_index <= last_token_index; ++token_index) {
    auto token_value = ParseUint64Token(tokens[token_index]);
    if (!token_value.has_value()) {
      return std::unexpected(token_value.error());
    }
    total_ticks += *token_value;
    if (token_index == kCpuIdleTokenIndex || token_index == kCpuIoWaitTokenIndex) {
      idle_ticks += *token_value;
    }
  }

  return CpuSnapshot{.total = total_ticks, .idle = idle_ticks};
}

}  // namespace

auto ParseCpuSnapshotFromContents(std::string_view contents, std::string_view line_prefix)
    -> std::expected<CpuSnapshot, astl_status_code> {
  auto tokens_or_error = ReadPrefixedLineTokens(contents, line_prefix);
  if (!tokens_or_error.has_value()) {
    return std::unexpected(tokens_or_error.error());
  }
  return ParseCpuSnapshotTokens(*tokens_or_error);
}

auto ParseCpuSnapshotsFromContents(std::string_view contents) -> std::expected<CpuSnapshotMap, astl_status_code> {
  CpuSnapshotMap snapshots;
  size_t         position = 0;
  while (position <= contents.size()) {
    const size_t end = contents.find('\n', position);
    const auto   line =
        Trim(contents.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position));
    const auto prefix_end  = line.find_first_of(" \t");
    const auto line_prefix = line.substr(0, prefix_end);
    if (IsCpuLinePrefix(line_prefix)) {
      auto snapshot_or_error = ParseCpuSnapshotTokens(SplitWhitespace(line));
      if (!snapshot_or_error.has_value()) {
        return std::unexpected(snapshot_or_error.error());
      }
      snapshots.emplace(line_prefix, *snapshot_or_error);
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }

  if (snapshots.empty()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return snapshots;
}

}  // namespace astl::procfs::detail
