// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "numa/numa_source.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
namespace astl::numa {
auto Parse(std::string_view text) -> std::expected<Counts, astl_status_code> {
  Counts                           values{};
  std::array<bool, kFields.size()> seen{};
  std::istringstream               input{std::string{text}};
  std::string                      line;
  while (std::getline(input, line)) {
    std::istringstream tokens{line};
    std::string        key;
    std::string        value;
    std::string        extra;
    if (!(tokens >> key)) {
      continue;
    }
    // MSVC uses a checked iterator here rather than a raw pointer.
    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto field = std::find(kFields.begin(), kFields.end(), key);
    if (field == kFields.end()) {
      continue;
    }
    const auto index = static_cast<size_t>(field - kFields.begin());
    if (seen.at(index) || !(tokens >> value) || (tokens >> extra)) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    // from_chars accepts a pointer range; both endpoints are inside the string.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto* limit       = value.data() + value.size();
    const auto [end, error] = std::from_chars(value.data(), limit, values.at(index));
    if (error != std::errc{} || end != limit) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    seen.at(index) = true;
  }
  if (!std::ranges::all_of(seen, [](bool present) { return present; })) {
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return values;
}
auto Discover(const FileInterface& files) -> std::expected<std::vector<std::string>, astl_status_code> {
  std::vector<std::string> nodes;
  auto                     exists = files.IsValid(files.GetBasePath());
  if (!exists) {
    return std::unexpected(exists.error());
  }
  if (!*exists) {
    return nodes;
  }
  auto dirs = files.GetSubdirectories();
  if (!dirs) {
    return std::unexpected(dirs.error());
  }
  for (const auto& dir : *dirs) {
    const auto name = dir.path().filename().string();
    if (!name.starts_with("node") || name.size() == 4 || name.find_first_not_of("0123456789", 4) != std::string::npos) {
      continue;
    }
    nodes.push_back(name);
  }
  // Validate every identified node so aggregate metrics never omit a failed node.
  auto snapshot = Read(files, nodes);
  if (!snapshot) {
    return std::unexpected(snapshot.error());
  }
  std::sort(nodes.begin(), nodes.end());
  return nodes;
}
auto Read(const FileInterface& files, const std::vector<std::string>& nodes)
    -> std::expected<Snapshot, astl_status_code> {
  Snapshot result;
  for (const auto& node : nodes) {
    std::string text;
    const auto  status = files.Read(std::filesystem::path{node} / "numastat", text);
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
    auto counts = Parse(text);
    if (!counts) {
      return std::unexpected(counts.error());
    }
    result.emplace(node, *counts);
  }
  return result;
}
auto Delta(const Snapshot& previous, const Snapshot& current, const std::string& node) -> std::optional<Counts> {
  if (previous.empty() || previous.size() != current.size()) {
    return std::nullopt;
  }
  Counts delta{};
  bool   found = false;
  for (const auto& [name, counts] : current) {
    if (!node.empty() && node != name) {
      continue;
    }
    const auto old = previous.find(name);
    if (old == previous.end()) {
      return std::nullopt;
    }
    found = true;
    for (size_t i = 0; i < delta.size(); ++i) {
      if (counts.at(i) < old->second.at(i)) {
        return std::nullopt;
      }
      const auto increment = counts.at(i) - old->second.at(i);
      if (increment > std::numeric_limits<uint64_t>::max() - delta.at(i)) {
        return std::nullopt;
      }
      delta.at(i) += increment;
    }
  }
  return found ? std::optional{delta} : std::nullopt;
}
}  // namespace astl::numa
