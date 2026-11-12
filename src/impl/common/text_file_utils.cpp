// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/text_file_utils.hpp"

#include <string_view>

#include "astl_cxx_compat.hpp"
#include "astl_file_interface.hpp"
#include "procfs/procfs_utils.hpp"

namespace astl::text {

namespace {

auto SharedFileInterface() -> const FileInterface& {
  static const FileInterface file_interface{};
  return file_interface;
}

}  // namespace

auto ReadTextFile(const std::filesystem::path& path) -> astl::compat::optional<std::string> {
  std::string contents;
  if (SharedFileInterface().Read(path, contents) != ASTL_STATUS_SUCCESS || contents.empty()) {
    return astl::compat::nullopt;
  }
  return contents;
}

auto ReadFirstTrimmedLine(const std::filesystem::path& path) -> astl::compat::optional<std::string> {
  const auto contents = ReadTextFile(path);
  if (!contents.has_value()) {
    return astl::compat::nullopt;
  }

  const auto newline = contents->find('\n');
  const auto line =
      std::string_view{*contents}.substr(0, newline == std::string::npos ? std::string_view::npos : newline);
  const auto trimmed = procfs::Trim(line);
  if (trimmed.empty()) {
    return astl::compat::nullopt;
  }
  return std::string{trimmed};
}

auto ReadFirstAvailableLine(std::initializer_list<std::filesystem::path> paths) -> astl::compat::optional<std::string> {
  for (const auto& path : paths) {
    auto value = ReadFirstTrimmedLine(path);
    if (value.has_value()) {
      return value;
    }
  }
  return astl::compat::nullopt;
}

}  // namespace astl::text
