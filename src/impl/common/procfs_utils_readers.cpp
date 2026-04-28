// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/procfs_utils_readers.hpp"

#include <algorithm>
#include <string_view>

#include "common/procfs_utils_readers_primitives.hpp"

namespace astl::procfs::detail {

namespace {

constexpr size_t kCpuSnapshotRequiredTokenCount = 5;
constexpr size_t kCpuIdleTokenIndex             = 4;
constexpr size_t kCpuIoWaitTokenIndex           = 5;

auto ParseCpuTickToken(const std::vector<std::string>& tokens, size_t token_index)
    -> std::expected<uint64_t, astl_status_code> {
  return ParseUint64Token(tokens[token_index]);
}

}  // namespace

auto ReadKeyValueField(std::string_view contents, const KeyValueField& field)
    -> std::expected<AstlValue, astl_status_code> {
  size_t position = 0;
  while (position <= contents.size()) {
    const size_t end = contents.find('\n', position);
    auto line = contents.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      const auto key = Trim(line.substr(0, colon));
      if (key == field.field_name) {
        auto value_tokens = SplitWhitespace(line.substr(colon + 1));
        if (value_tokens.empty()) {
          return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
        }
        return ParseAstlValue(value_tokens.front(), field.raw_value_type);
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

auto ReadTokenField(std::string_view contents, const TokenField& field) -> std::expected<AstlValue, astl_status_code> {
  auto tokens_or_error = ReadPrefixedLineTokens(contents, field.line_prefix);
  if (!tokens_or_error.has_value()) {
    return std::unexpected(tokens_or_error.error());
  }
  const auto& tokens = *tokens_or_error;
  if (field.token_index >= tokens.size()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return ParseAstlValue(tokens[field.token_index], field.raw_value_type);
}

auto ReadSplitTokenField(std::string_view contents, const SplitTokenField& field)
    -> std::expected<AstlValue, astl_status_code> {
  auto tokens_or_error = ReadPrefixedLineTokens(contents, field.line_prefix);
  if (!tokens_or_error.has_value()) {
    return std::unexpected(tokens_or_error.error());
  }
  const auto& tokens = *tokens_or_error;
  if (field.token_index >= tokens.size()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  const auto& token = tokens[field.token_index];
  const auto  split = token.find(field.delimiter);
  if (split == std::string::npos) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  const auto part = field.part == SplitTokenPart::BEFORE_DELIMITER ? std::string_view{token}.substr(0, split)
                                                                   : std::string_view{token}.substr(split + 1);
  return ParseAstlValue(part, field.raw_value_type);
}

auto ReadTokenSumField(std::string_view contents, const TokenSumField& field)
    -> std::expected<AstlValue, astl_status_code> {
  auto tokens_or_error = ReadPrefixedLineTokens(contents, field.line_prefix);
  if (!tokens_or_error.has_value()) {
    return std::unexpected(tokens_or_error.error());
  }
  const auto& tokens = *tokens_or_error;
  if (field.token_start_index > field.token_end_index || field.token_end_index >= tokens.size()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  uint64_t total{0};
  for (size_t token_index = field.token_start_index; token_index <= field.token_end_index; ++token_index) {
    auto value_or_error = ParseUint64Token(tokens[token_index]);
    if (!value_or_error.has_value()) {
      return std::unexpected(value_or_error.error());
    }
    total += value_or_error.value();
  }

  return AstlValue{total};
}

auto ReadMemUsedField(std::string_view contents, const MemUsedField& /*field*/)
    -> std::expected<AstlValue, astl_status_code> {
  auto usage_or_error = ReadMemUsageInKilobytes(contents);
  if (!usage_or_error.has_value()) {
    return std::unexpected(usage_or_error.error());
  }

  return AstlValue{usage_or_error->used_kb};
}

auto ReadMemUsedPercentField(std::string_view contents, const MemUsedPercentField& /*field*/)
    -> std::expected<AstlValue, astl_status_code> {
  auto usage_or_error = ReadMemUsageInKilobytes(contents);
  if (!usage_or_error.has_value()) {
    return std::unexpected(usage_or_error.error());
  }

  if (usage_or_error->total_kb == 0) {
    return AstlValue{0.0};
  }

  const auto used_percent =
      (static_cast<double>(usage_or_error->used_kb) / static_cast<double>(usage_or_error->total_kb)) * 100.0;
  return AstlValue{used_percent};
}

auto ParseCpuSnapshotFromContents(std::string_view contents, std::string_view line_prefix)
    -> std::expected<CpuSnapshot, astl_status_code> {
  auto tokens_or_error = ReadPrefixedLineTokens(contents, line_prefix);
  if (!tokens_or_error.has_value()) {
    return std::unexpected(tokens_or_error.error());
  }

  const auto& tokens = *tokens_or_error;
  if (tokens.size() < kCpuSnapshotRequiredTokenCount) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  uint64_t         total_ticks{0};
  uint64_t         idle_ticks{0};
  astl_status_code parse_error = ASTL_STATUS_SUCCESS;

  // Sum only the canonical CPU fields: user, nice, system, idle, iowait, irq, softirq, steal.
  // This avoids double-counting guest and guest_nice, which are already included in user/nice.
  const size_t max_total_field_index = std::min(tokens.size() - 1, static_cast<size_t>(8));
  for (size_t token_index = 1; token_index <= max_total_field_index; ++token_index) {
    auto token_value = ParseCpuTickToken(tokens, token_index);
    if (!token_value.has_value()) {
      parse_error = token_value.error();
      break;
    }
    total_ticks += token_value.value();
  }

  if (parse_error == ASTL_STATUS_SUCCESS) {
    auto idle_ticks_or_error = ParseCpuTickToken(tokens, kCpuIdleTokenIndex);
    if (!idle_ticks_or_error.has_value()) {
      parse_error = idle_ticks_or_error.error();
    } else {
      idle_ticks = idle_ticks_or_error.value();
    }
  }

  if (parse_error == ASTL_STATUS_SUCCESS && tokens.size() > kCpuIoWaitTokenIndex) {
    auto iowait_ticks_or_error = ParseCpuTickToken(tokens, kCpuIoWaitTokenIndex);
    if (!iowait_ticks_or_error.has_value()) {
      parse_error = iowait_ticks_or_error.error();
    } else {
      idle_ticks += iowait_ticks_or_error.value();
    }
  }

  if (parse_error != ASTL_STATUS_SUCCESS) {
    return std::unexpected(parse_error);
  }

  return CpuSnapshot{.total = total_ticks, .idle = idle_ticks};
}

}  // namespace astl::procfs::detail
