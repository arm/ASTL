// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <limits>
#include <sstream>

#include "procfs/procfs_system_utils.hpp"
#include "procfs/procfs_utils_readers_primitives.hpp"

namespace astl::procfs::detail {
namespace {
constexpr size_t   kDiskReadOperationsIndex  = 3;
constexpr size_t   kDiskWriteOperationsIndex = 7;
constexpr size_t   kDiskReadSectorsIndex     = 5;
constexpr size_t   kDiskWriteSectorsIndex    = 9;
constexpr uint64_t kBytesPerDiskSector       = 512;
constexpr size_t   kNetworkFieldCount        = 16;
constexpr size_t   kNetworkTxBytesIndex      = 8;
auto ParseDiskField(const std::vector<std::string>& tokens, const SystemField& field, std::string* source_identity)
    -> std::expected<AstlValue, astl_status_code> {
  size_t index = 0;
  if (field.kind == "read_operations") {
    index = kDiskReadOperationsIndex;
  }
  if (field.kind == "write_operations") {
    index = kDiskWriteOperationsIndex;
  }
  if (field.kind == "read_bytes") {
    index = kDiskReadSectorsIndex;
  }
  if (field.kind == "write_bytes") {
    index = kDiskWriteSectorsIndex;
  }
  if (index == 0 || tokens.size() < kDiskStatsMinTokenCount || !ParseUint64Token(tokens[0]) ||
      !ParseUint64Token(tokens[1])) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto           value = ParseUint64Token(tokens[index]);
  const uint64_t scale = field.kind.ends_with("bytes") ? kBytesPerDiskSector : 1;
  if (!value || *value > std::numeric_limits<uint64_t>::max() / scale) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (source_identity) {
    *source_identity = tokens[0] + ':' + tokens[1];
  }
  return AstlValue{*value * scale};
}
}  // namespace

auto ReadDiskField(std::string_view contents, const SystemField& field, std::string* source_identity,
                   bool* missing_label) -> std::expected<AstlValue, astl_status_code> {
  std::istringstream stream{std::string{contents}};
  std::string        line;
  while (std::getline(stream, line)) {
    const auto tokens = SplitWhitespace(line);
    if (tokens.size() >= 3 && tokens[2] == field.label) {
      return ParseDiskField(tokens, field, source_identity);
    }
  }
  if (missing_label) {
    *missing_label = true;
  }
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

auto ReadNetworkField(std::string_view contents, const SystemField& field, bool* missing_label)
    -> std::expected<AstlValue, astl_status_code> {
  std::istringstream stream{std::string{contents}};
  std::string        line;
  while (std::getline(stream, line)) {
    // The colon may touch the byte count; split it before tokenizing the fields.
    const auto colon = line.find(':');
    if (colon == std::string::npos || Trim(std::string_view{line}.substr(0, colon)) != field.label) {
      continue;
    }
    const auto tokens = SplitWhitespace(std::string_view{line}.substr(colon + 1));
    if (tokens.size() != kNetworkFieldCount || (field.kind != "rx_bytes" && field.kind != "tx_bytes")) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return ParseAstlValue(tokens[field.kind == "rx_bytes" ? 0 : kNetworkTxBytesIndex], ASTL_VALUE_UINT64);
  }
  if (missing_label) {
    *missing_label = true;
  }
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}
}  // namespace astl::procfs::detail
