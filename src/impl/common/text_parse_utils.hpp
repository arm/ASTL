// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_TEXT_PARSE_UTILS_HPP_
#define ASTL_TEXT_PARSE_UTILS_HPP_

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

#include "common/procfs_utils.hpp"

namespace astl::text {

inline auto ParseUint64(std::string_view text) -> std::optional<uint64_t> {
  const auto trimmed = procfs::Trim(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  uint64_t    value{0};
  const auto* first  = trimmed.data();
  const auto* last   = trimmed.data() + trimmed.size();
  const auto  result = std::from_chars(first, last, value);
  if (result.ec != std::errc{} || result.ptr != last) {
    return std::nullopt;
  }
  return value;
}

inline auto ParseLeadingUint64(std::string_view text) -> std::optional<uint64_t> {
  const auto trimmed = procfs::Trim(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  const auto end = trimmed.find_first_of(" \t\n\r");
  return ParseUint64(trimmed.substr(0, end));
}

}  // namespace astl::text

#endif  // ASTL_TEXT_PARSE_UTILS_HPP_
