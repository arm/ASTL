// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <filesystem>
#include <string_view>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl_utils.hpp"
#include "common/scmi/scmi_constants.hpp"
#include "config/astl_configuration.hpp"

namespace fs = std::filesystem;

namespace {

void CreateConfigTree(const fs::path& config_dir) {
  std::error_code ec;
  fs::create_directories(config_dir / "metrics", ec);
  REQUIRE(!ec);
  fs::create_directories(config_dir / "scmi" / "public", ec);
  REQUIRE(!ec);
}

auto CreateConfigurationWithRootOverride(std::string_view config_dir_name, astl::EnvVar root_env_var,
                                         const fs::path& root_override)
    -> std::expected<astl::AstlConfiguration, astl_status_code> {
  const fs::path config_dir = fs::temp_directory_path() / std::string{config_dir_name};
  TempFileGuard  config_guard(config_dir);
  CreateConfigTree(config_dir);

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, config_dir.string());
  EnvVarGuard root_guard(root_env_var, root_override.string());

  return astl::AstlConfiguration::CreateConfiguration();
}

}  // namespace

TEST_CASE("AstlConfiguration::CreateConfiguration uses valid ASTL_CONFIG_DIR override", "[AstlConfiguration]") {
  const fs::path config_dir = fs::temp_directory_path() / "astl_config_env_override";
  TempFileGuard  config_guard(config_dir);
  CreateConfigTree(config_dir);

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, config_dir.string());

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();

  REQUIRE(configuration_result.has_value());
  REQUIRE(configuration_result->config_dir_path == config_dir);
  REQUIRE(configuration_result->metrics_dir_path == config_dir / "metrics");
  REQUIRE(configuration_result->scmi_specification_dir == config_dir / "scmi" / "public");
}

TEST_CASE("AstlConfiguration::CreateConfiguration rejects invalid ASTL_CONFIG_DIR override", "[AstlConfiguration]") {
  const fs::path  invalid_dir = fs::temp_directory_path() / "astl_missing_config_dir";
  TempFileGuard   config_guard(invalid_dir);
  std::error_code ec;
  fs::remove_all(invalid_dir, ec);

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, invalid_dir.string());

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();

  REQUIRE_FALSE(configuration_result.has_value());
  REQUIRE(configuration_result.error() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("AstlConfiguration::CreateConfiguration falls back to XDG_DATA_HOME/astl/config on Linux",
          "[AstlConfiguration]") {
  const fs::path xdg_root   = fs::temp_directory_path() / "astl_xdg_data_home";
  const fs::path config_dir = xdg_root / "astl" / "config";
  TempFileGuard  xdg_guard(xdg_root);
  CreateConfigTree(config_dir);

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, "");
  EnvVarGuard xdg_guard_var(astl::EnvVar::XDG_DATA_HOME, xdg_root.string());

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();

  REQUIRE(configuration_result.has_value());
  REQUIRE(configuration_result->config_dir_path == config_dir);
}

TEST_CASE("AstlConfiguration::CreateConfiguration falls back to HOME/.local/share/astl/config on Linux",
          "[AstlConfiguration]") {
  const fs::path home_root  = fs::temp_directory_path() / "astl_home_data_home";
  const fs::path config_dir = home_root / ".local" / "share" / "astl" / "config";
  TempFileGuard  home_guard(home_root);
  CreateConfigTree(config_dir);

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, "");
  EnvVarGuard xdg_guard_var(astl::EnvVar::XDG_DATA_HOME, "");
  EnvVarGuard home_guard_var(astl::EnvVar::HOME, home_root.string());

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();

  REQUIRE(configuration_result.has_value());
  REQUIRE(configuration_result->config_dir_path == config_dir);
}

TEST_CASE("AstlConfiguration::CreateConfiguration honors ASTL_SCMI_SYSFS_TELEMETRY_ROOT override",
          "[AstlConfiguration]") {
  const fs::path sysfs_root = fs::temp_directory_path() / "astl_fake_scmi_sysfs";
  TempFileGuard  sysfs_guard(sysfs_root);

  auto configuration_result = CreateConfigurationWithRootOverride(
      "astl_config_sysfs_override", astl::EnvVar::ASTL_SCMI_SYSFS_TELEMETRY_ROOT, sysfs_root);

  REQUIRE(configuration_result.has_value());
  REQUIRE(configuration_result->scmi_sysfs_telemetry_root_path == sysfs_root);
}

TEST_CASE("AstlConfiguration::CreateConfiguration uses default SCMI sysfs root when override is unset",
          "[AstlConfiguration]") {
  const fs::path config_dir = fs::temp_directory_path() / "astl_config_default_sysfs";
  TempFileGuard  config_guard(config_dir);
  CreateConfigTree(config_dir);

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, config_dir.string());
  EnvVarGuard sysfs_guard_var(astl::EnvVar::ASTL_SCMI_SYSFS_TELEMETRY_ROOT, "");

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();

  REQUIRE(configuration_result.has_value());
  REQUIRE(configuration_result->scmi_sysfs_telemetry_root_path == astl::kDefaultScmiSysfsTelemetryRootPath);
}

TEST_CASE("AstlConfiguration::CreateConfiguration honors ASTL_SCMI_IOCTL_DEV_ROOT override", "[AstlConfiguration]") {
  const fs::path ioctl_device_dir = fs::temp_directory_path() / "astl_fake_scmi_ioctl";
  TempFileGuard  ioctl_guard(ioctl_device_dir);

  auto configuration_result = CreateConfigurationWithRootOverride(
      "astl_config_ioctl_override", astl::EnvVar::ASTL_SCMI_IOCTL_DEV_ROOT, ioctl_device_dir);

  REQUIRE(configuration_result.has_value());
  REQUIRE(configuration_result->scmi_ioctl_device_root_path == ioctl_device_dir);
}
