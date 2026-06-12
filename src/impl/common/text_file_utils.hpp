// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_TEXT_FILE_UTILS_HPP_
#define ASTL_TEXT_FILE_UTILS_HPP_

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>

namespace astl::text {

auto ReadTextFile(const std::filesystem::path& path) -> std::optional<std::string>;

auto ReadFirstTrimmedLine(const std::filesystem::path& path) -> std::optional<std::string>;

auto ReadFirstAvailableLine(std::initializer_list<std::filesystem::path> paths) -> std::optional<std::string>;

}  // namespace astl::text

#endif  // ASTL_TEXT_FILE_UTILS_HPP_
