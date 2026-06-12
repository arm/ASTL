// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/text_file_utils.hpp"

#include <string_view>

#include "astl_file_interface.hpp"
#include "common/procfs_utils.hpp"

namespace astl::text {

namespace {

auto SharedFileInterface() -> const FileInterface& {
  static const FileInterface file_interface{};
  return file_interface;
}

}  // namespace

auto ReadTextFile(const std::filesystem::path& path) -> std::optional<std::string> {
  std::string contents;
  if (SharedFileInterface().Read(path, contents) != ASTL_STATUS_SUCCESS || contents.empty()) {
    return std::nullopt;
  }
  return contents;
}

auto ReadFirstTrimmedLine(const std::filesystem::path& path) -> std::optional<std::string> {
  const auto contents = ReadTextFile(path);
  if (!contents.has_value()) {
    return std::nullopt;
  }

  const auto newline = contents->find('\n');
  const auto line =
      std::string_view{*contents}.substr(0, newline == std::string::npos ? std::string_view::npos : newline);
  const auto trimmed = procfs::Trim(line);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  return std::string{trimmed};
}

auto ReadFirstAvailableLine(std::initializer_list<std::filesystem::path> paths) -> std::optional<std::string> {
  for (const auto& path : paths) {
    auto value = ReadFirstTrimmedLine(path);
    if (value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

}  // namespace astl::text
