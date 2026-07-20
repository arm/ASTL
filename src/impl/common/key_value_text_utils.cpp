// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/key_value_text_utils.hpp"

#include "common/procfs_utils.hpp"
#include "common/text_file_utils.hpp"

namespace astl::text {

auto ParseKeyValueLine(std::string_view line, char separator) -> std::optional<KeyValueLineView> {
  const auto split = line.find(separator);
  if (split == std::string_view::npos) {
    return std::nullopt;
  }

  const auto key   = procfs::Trim(line.substr(0, split));
  const auto value = procfs::Trim(line.substr(split + 1));
  if (key.empty() || value.empty()) {
    return std::nullopt;
  }

  return KeyValueLineView{.key = key, .value = value};
}

auto ParseKeyValueText(std::string_view text, char separator) -> KeyValueMap {
  KeyValueMap values;
  size_t      position = 0;
  while (position <= text.size()) {
    const size_t end  = text.find('\n', position);
    const auto   line = text.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
    if (const auto parsed = ParseKeyValueLine(line, separator); parsed.has_value()) {
      values.try_emplace(std::string{parsed->key}, std::string{parsed->value});
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }
  return values;
}

auto FindFirstKeyValue(const KeyValueMap& values, std::initializer_list<std::string_view> keys) -> std::string {
  for (const auto key : keys) {
    const auto it = values.find(key);
    if (it != values.end()) {
      return it->second;
    }
  }
  return {};
}

auto FindFirstKeyValue(std::string_view text, std::initializer_list<std::string_view> keys, char separator)
    -> std::string {
  size_t position = 0;
  while (position <= text.size()) {
    const size_t end  = text.find('\n', position);
    const auto   line = text.substr(position, end == std::string_view::npos ? std::string_view::npos : end - position);
    if (const auto parsed = ParseKeyValueLine(line, separator); parsed.has_value()) {
      for (const auto key : keys) {
        if (parsed->key == key) {
          return std::string{parsed->value};
        }
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }
  return {};
}

auto ReadFirstMatchingKeyValueFromFile(const std::filesystem::path& path, std::initializer_list<std::string_view> keys,
                                       char separator) -> std::optional<std::string> {
  const auto contents = ReadTextFile(path);
  if (!contents.has_value()) {
    return std::nullopt;
  }

  auto value = FindFirstKeyValue(*contents, keys, separator);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace astl::text
