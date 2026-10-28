// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/configuration_manager.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"

namespace astl {
namespace ConfigurationManager {

namespace fs = std::filesystem;

namespace {
auto GetOverrideMutex() -> std::mutex& {
  static std::mutex override_mtx;
  return override_mtx;
}

auto GetLoadFilePathOverrideStorage() -> std::optional<fs::path>& {
  static std::optional<fs::path> load_file_path_override;
  return load_file_path_override;
}

}  // namespace

auto GetConfiguration() -> std::expected<AstlConfiguration, astl_status_code> {
  return AstlConfiguration::CreateConfiguration();
};

auto SetLoadFilePathOverride(const std::optional<fs::path>& load_file_path) -> void {
  std::scoped_lock lock{GetOverrideMutex()};
  GetLoadFilePathOverrideStorage() = load_file_path;
}

auto GetLoadFilePathOverride() -> std::optional<fs::path> {
  std::scoped_lock lock{GetOverrideMutex()};
  return GetLoadFilePathOverrideStorage();
}

}  // namespace ConfigurationManager

}  // namespace astl
