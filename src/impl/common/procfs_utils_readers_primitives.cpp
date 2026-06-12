// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/procfs_utils_readers_primitives.hpp"

#include <charconv>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

#include "astl_logger.hpp"
#include "common/procfs_utils_readers.hpp"
#include "common/text_parse_utils.hpp"

namespace astl::procfs::detail {

namespace {

template <typename UnsignedIntT>
auto ParseUnsignedAstlValue(std::string_view trimmed) -> std::expected<AstlValue, astl_status_code> {
  UnsignedIntT value{0};
  const auto*  begin  = trimmed.data();
  const auto*  end    = trimmed.data() + trimmed.size();
  const auto   result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return AstlValue{value};
}

template <typename FloatT, typename ParseFn>
auto ParseFloatingAstlValue(std::string_view trimmed, ParseFn&& parse_fn)
    -> std::expected<AstlValue, astl_status_code> {
  try {
    size_t            parsed_chars{0};
    const std::string token_copy{trimmed};
    const FloatT      value = std::forward<ParseFn>(parse_fn)(token_copy, &parsed_chars);
    if (parsed_chars != token_copy.size()) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return AstlValue{value};
  } catch (const std::exception&) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

auto FindLineByPrefix(std::string_view contents, std::string_view line_prefix) -> std::optional<std::string_view> {
  size_t position = 0;
  while (position <= contents.size()) {
    const size_t end = contents.find('\n', position);
    auto line = contents.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
    const auto trimmed = Trim(line);
    if (!trimmed.empty()) {
      if (line_prefix.empty()) {
        return trimmed;
      }
      auto tokens = SplitWhitespace(trimmed);
      if (!tokens.empty() && tokens.front() == line_prefix) {
        return trimmed;
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }
  return std::nullopt;
}

auto ReadRequiredMeminfoValueInKilobytes(std::string_view contents, std::string_view field_name)
    -> std::expected<uint64_t, astl_status_code> {
  auto value_or_error =
      ReadKeyValueField(contents, KeyValueField{"meminfo", std::string{field_name}, ASTL_VALUE_UINT64});
  if (!value_or_error.has_value()) {
    return std::unexpected(value_or_error.error());
  }
  auto value_as_int = value_or_error->ToInt64();
  if (!value_as_int.has_value() || *value_as_int < 0) {
    return std::unexpected(value_as_int.has_value() ? ASTL_STATUS_BAD_CONFIGURATION : value_as_int.error());
  }
  return static_cast<uint64_t>(*value_as_int);
}

}  // namespace

auto ParseAstlValue(std::string_view token, astl_value_type_t raw_value_type)
    -> std::expected<AstlValue, astl_status_code> {
  const auto trimmed = Trim(token);
  if (trimmed.empty()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  std::expected<AstlValue, astl_status_code> parsed_value = std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
  switch (raw_value_type) {
    case ASTL_VALUE_UINT64:
      parsed_value = ParseUnsignedAstlValue<uint64_t>(trimmed);
      break;
    case ASTL_VALUE_UINT32:
      parsed_value = ParseUnsignedAstlValue<uint32_t>(trimmed);
      break;
    case ASTL_VALUE_FLOAT64:
      parsed_value = ParseFloatingAstlValue<double>(
          trimmed, [](const std::string& text, size_t* parsed_chars) { return std::stod(text, parsed_chars); });
      break;
    case ASTL_VALUE_FLOAT32:
      parsed_value = ParseFloatingAstlValue<float>(
          trimmed, [](const std::string& text, size_t* parsed_chars) { return std::stof(text, parsed_chars); });
      break;
    default:
      ASTL_LOG_ERROR("procfs ParseAstlValue: unsupported raw value type {}", static_cast<int>(raw_value_type));
      break;
  }
  return parsed_value;
}

auto ParseUint64Token(std::string_view token) -> std::expected<uint64_t, astl_status_code> {
  auto value = text::ParseUint64(token);
  if (!value.has_value()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return *value;
}

auto ReadPrefixedLineTokens(std::string_view contents, std::string_view line_prefix)
    -> std::expected<std::vector<std::string>, astl_status_code> {
  auto line = FindLineByPrefix(contents, line_prefix);
  if (!line.has_value()) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return SplitWhitespace(*line);
}

auto ReadMemUsageInKilobytes(std::string_view contents) -> std::expected<MemUsageInKilobytes, astl_status_code> {
  auto total_kb_or_error     = ReadRequiredMeminfoValueInKilobytes(contents, "MemTotal");
  auto available_kb_or_error = ReadRequiredMeminfoValueInKilobytes(contents, "MemAvailable");
  if (!total_kb_or_error.has_value()) {
    return std::unexpected(total_kb_or_error.error());
  }
  if (!available_kb_or_error.has_value()) {
    return std::unexpected(available_kb_or_error.error());
  }

  const auto used_kb = available_kb_or_error.value() >= total_kb_or_error.value()
                           ? uint64_t{0}
                           : total_kb_or_error.value() - available_kb_or_error.value();
  return MemUsageInKilobytes{.total_kb = total_kb_or_error.value(), .used_kb = used_kb};
}

}  // namespace astl::procfs::detail
