// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "astl/astl_errors.h"
namespace astl::config {
// An immutable snapshot of one selected configuration source.
// Paths exposed to loaders are logical paths, never filesystem search roots.
class ConfigSource {
 public:
  // An empty binary_path discovers the loaded module; callers may supply a known binary location.
  static auto Create(const std::filesystem::path& binary_path = {})
      -> std::expected<std::shared_ptr<const ConfigSource>, astl_status_code>;
  auto Load(const std::filesystem::path& path) const -> std::expected<nlohmann::json, astl_status_code>;
  auto Exists(const std::filesystem::path& path) const -> bool;
  auto Files(const std::filesystem::path& root, bool recursive = true) const -> std::vector<std::filesystem::path>;

 private:
  std::map<std::string, nlohmann::json> documents_;
};
}  // namespace astl::config
