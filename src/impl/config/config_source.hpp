// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_cxx_compat.hpp"
#include "astl_json.hpp"
namespace astl::config {
// An immutable snapshot of one selected configuration source.
// Paths exposed to loaders are logical paths, never filesystem search roots.
class ConfigSource {
 public:
  // An empty binary_path discovers the loaded module; callers may supply a known binary location.
  static auto Create(const std::filesystem::path& binary_path = {})
      -> astl::compat::expected<std::shared_ptr<const ConfigSource>, astl_status_code>;
  auto Load(const std::filesystem::path& path) const -> astl::compat::expected<nlohmann::json, astl_status_code>;
  auto Exists(const std::filesystem::path& path) const -> bool;
  auto Files(const std::filesystem::path& root, bool recursive = true) const -> std::vector<std::filesystem::path>;

 private:
  std::map<std::string, nlohmann::json> documents_;
};
}  // namespace astl::config
