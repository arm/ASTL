// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_KEY_VALUE_TEXT_UTILS_HPP_
#define ASTL_KEY_VALUE_TEXT_UTILS_HPP_

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace astl::text {

struct KeyValueLineView {
  std::string_view key;
  std::string_view value;
};

using KeyValueMap = std::map<std::string, std::string, std::less<>>;

auto ParseKeyValueLine(std::string_view line, char separator = ':') -> std::optional<KeyValueLineView>;

auto ParseKeyValueText(std::string_view text, char separator = ':') -> KeyValueMap;

auto FindFirstKeyValue(const KeyValueMap& values, std::initializer_list<std::string_view> keys) -> std::string;

auto FindFirstKeyValue(std::string_view text, std::initializer_list<std::string_view> keys, char separator = ':')
    -> std::string;

auto ReadFirstMatchingKeyValueFromFile(const std::filesystem::path& path, std::initializer_list<std::string_view> keys,
                                       char separator = ':') -> std::optional<std::string>;

}  // namespace astl::text

#endif  // ASTL_KEY_VALUE_TEXT_UTILS_HPP_
