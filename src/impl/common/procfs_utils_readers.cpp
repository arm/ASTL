// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/procfs_utils_readers.hpp"

#include <string_view>

#include "common/key_value_text_utils.hpp"
#include "common/procfs_utils_readers_primitives.hpp"

namespace astl::procfs::detail {

auto ReadKeyValueField(std::string_view contents, const KeyValueField& field)
    -> std::expected<AstlValue, astl_status_code> {
  size_t position = 0;
  while (position <= contents.size()) {
    const size_t end = contents.find('\n', position);
    auto line = contents.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
    if (const auto parsed = text::ParseKeyValueLine(line); parsed.has_value()) {
      if (parsed->key == field.field_name) {
        auto value_tokens = SplitWhitespace(parsed->value);
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

}  // namespace astl::procfs::detail
