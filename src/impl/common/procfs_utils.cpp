// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/procfs_utils.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "astl_logger.hpp"
#include "common/procfs_utils_readers.hpp"

namespace astl::procfs {

auto SplitWhitespace(std::string_view line) -> std::vector<std::string> {
  std::vector<std::string> tokens;
  size_t                   position = 0;
  while (position < line.size()) {
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])) != 0) {
      ++position;
    }
    if (position >= line.size()) {
      break;
    }
    size_t end = position;
    while (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])) == 0) {
      ++end;
    }
    tokens.emplace_back(line.substr(position, end - position));
    position = end;
  }
  return tokens;
}

auto Trim(std::string_view text) -> std::string_view {
  size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

auto ReadField(const FileInterface& file_interface, const FieldDescriptor& field_descriptor)
    -> std::expected<AstlValue, astl_status_code> {
  const auto  relative_path = std::visit([](const auto& field) { return field.relative_path; }, field_descriptor);
  std::string contents;
  const auto  status = file_interface.Read(relative_path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }

  return std::visit(
      [&contents](const auto& field) -> std::expected<AstlValue, astl_status_code> {
        using FieldT = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<FieldT, KeyValueField>) {
          return detail::ReadKeyValueField(contents, field);
        } else if constexpr (std::is_same_v<FieldT, TokenField>) {
          return detail::ReadTokenField(contents, field);
        } else if constexpr (std::is_same_v<FieldT, SplitTokenField>) {
          return detail::ReadSplitTokenField(contents, field);
        } else if constexpr (std::is_same_v<FieldT, TokenSumField>) {
          return detail::ReadTokenSumField(contents, field);
        } else if constexpr (std::is_same_v<FieldT, MemUsedField>) {
          return detail::ReadMemUsedField(contents, field);
        } else if constexpr (std::is_same_v<FieldT, MemUsedPercentField>) {
          return detail::ReadMemUsedPercentField(contents, field);
        } else {
          ASTL_LOG_ERROR("procfs ReadField: CpuUtilizationField requires collector-managed previous sample state");
          return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
        }
      },
      field_descriptor);
}

auto ReadCpuSnapshot(const FileInterface& file_interface, const CpuUtilizationField& field_descriptor)
    -> std::expected<CpuSnapshot, astl_status_code> {
  std::string contents;
  const auto  status = file_interface.Read(field_descriptor.relative_path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  return detail::ParseCpuSnapshotFromContents(contents, field_descriptor.line_prefix);
}

auto ReadCpuSnapshots(const FileInterface& file_interface, const std::filesystem::path& relative_path)
    -> std::expected<CpuSnapshotMap, astl_status_code> {
  std::string contents;
  const auto  status = file_interface.Read(relative_path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  return detail::ParseCpuSnapshotsFromContents(contents);
}

auto CalculateCpuUtilization(const CpuSnapshot& previous, const CpuSnapshot& current) -> double {
  if (current.total <= previous.total) {
    return 0.0;
  }

  const auto total_delta = current.total - previous.total;
  const auto idle_delta  = current.idle >= previous.idle ? current.idle - previous.idle : uint64_t{0};
  const auto busy_delta  = idle_delta >= total_delta ? uint64_t{0} : total_delta - idle_delta;
  const auto percent     = (static_cast<double>(busy_delta) / static_cast<double>(total_delta)) * 100.0;
  return std::clamp(percent, 0.0, 100.0);
}

}  // namespace astl::procfs
