// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "windows/windows_system_api.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <string_view>
#include <utility>

namespace astl {
namespace {
auto ParseProcessorIndex(std::string_view text) -> std::optional<unsigned> {
  unsigned value{};
  // from_chars requires a contiguous pointer range; this points one past the view.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto* end    = text.data() + text.size();
  const auto  parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}
}  // namespace

auto SortWindowsProcessorInstances(std::vector<std::string> instances) -> std::vector<std::string> {
  std::map<std::pair<unsigned, unsigned>, std::string> sorted;
  for (auto& instance : instances) {
    const auto comma = instance.find(',');
    if (comma == std::string::npos) {
      continue;
    }
    const auto view      = std::string_view{instance};
    const auto group     = ParseProcessorIndex(view.substr(0, comma));
    const auto processor = ParseProcessorIndex(view.substr(comma + 1));
    if (group && processor) {
      sorted.emplace(std::pair{*group, *processor}, std::move(instance));
    }
  }
  std::vector<std::string> result;
  for (auto& [identity, instance] : sorted) {
    result.push_back(std::move(instance));
  }
  return result;
}
}  // namespace astl
