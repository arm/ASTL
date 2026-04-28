// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef JSON_FILE_UTILS_HPP_
#define JSON_FILE_UTILS_HPP_

#include <expected>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "astl/astl.h"
#include "astl_logger.hpp"

namespace astl::config {

inline auto LoadJsonFile(const std::filesystem::path& json_file_path)
    -> std::expected<nlohmann::json, astl_status_code> {
  try {
    std::ifstream json_file{json_file_path};
    json_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return nlohmann::json::parse(json_file);
  } catch (const std::ifstream::failure& e) {
    ASTL_LOG_ERROR("Unable to open json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  } catch (const std::exception& e) {
    ASTL_LOG_ERROR("Unable to parse json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

template <typename SpecType>
inline auto TryParseJsonFile(const std::filesystem::path& json_file_path) -> std::expected<SpecType, astl_status_code> {
  auto json_or_error = LoadJsonFile(json_file_path);
  if (!json_or_error.has_value()) {
    return std::unexpected(json_or_error.error());
  }

  try {
    return json_or_error->template get<SpecType>();
  } catch (const std::exception& e) {
    ASTL_LOG_ERROR("Unable to parse json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

}  // namespace astl::config

#endif  // JSON_FILE_UTILS_HPP_
