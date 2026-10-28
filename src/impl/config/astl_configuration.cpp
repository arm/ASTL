// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/astl_configuration.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "collector/collector_backend.hpp"
#include "common/procfs_utils.hpp"
#include "common/scmi/scmi_constants.hpp"
#include "config/configuration_manager.hpp"

namespace astl {

auto CollectorSelection::IsEnabled(CollectorType collector_type) const -> bool {
  if (collector_type == CollectorType::ASTL_NATIVE) {
    return true;
  }
  if (enabled) {
    return std::ranges::find(*enabled, collector_type) != enabled->end();
  }
  const auto backends = BuiltinCollectorBackends();
  const auto backend  = std::ranges::find(backends, collector_type, &CollectorBackend::type);
  return backend != backends.end() && backend->discover != nullptr;
}

namespace {

auto TrimCollectorName(std::string_view value) -> std::string {
  // string_view iterators are not necessarily pointers (for example, with MSVC STL).
  // NOLINTNEXTLINE(readability-qualified-auto)
  const auto first = std::find_if_not(value.begin(), value.end(),
                                      [](unsigned char character) { return std::isspace(character) != 0; });
  // NOLINTNEXTLINE(readability-qualified-auto)
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
                      return std::isspace(character) != 0;
                    }).base();
  if (first >= last) {
    return {};
  }
  std::string result(first, last);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return result;
}

auto ParseCollectorSelection() -> std::expected<CollectorSelection, astl_status_code> {
  const std::string value = GetEnvVar(EnvVar::ASTL_COLLECTORS);
  if (TrimCollectorName(value).empty()) {
    return CollectorSelection{};
  }

  CollectorSelection selection{std::vector<CollectorType>{}};
  size_t             start = 0;
  while (start <= value.size()) {
    const auto end  = value.find(',', start);
    const auto name = TrimCollectorName(std::string_view{value}.substr(start, end - start));
    if (name.empty()) {
      ASTL_LOG_ERROR("ASTL_COLLECTORS contains an empty collector name: '{}'", value);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    const auto backends = BuiltinCollectorBackends();
    const auto backend  = std::ranges::find(backends, name, &CollectorBackend::name);
    if (backend == backends.end()) {
      ASTL_LOG_ERROR("Unknown collector '{}' in ASTL_COLLECTORS", name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    if (backend->discover != nullptr) {
      selection.enabled->push_back(backend->type);
    } else {
      ASTL_LOG_DEBUG("Skipping unavailable collector: {}", name);
    }

    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  if (selection.enabled->empty()) {
    ASTL_LOG_ERROR("None of the requested ASTL_COLLECTORS are available in this build");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return selection;
}

}  // namespace

/** @brief if ASTL_SCMI_SYSFS_TELEMETRY_ROOT is set, use that, else use default path */
static auto GetScmiSysFsTelemetryRootPath() -> std::filesystem::path {
  auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_SCMI_SYSFS_TELEMETRY_ROOT);
  if (!env_var_value.empty()) {
    return std::filesystem::path{env_var_value};
  }
  return std::filesystem::path{kDefaultScmiSysfsTelemetryRootPath};
}

/**
 * @brief Resolves the SCMI ioctl device root path.
 *
 * ASTL_SCMI_IOCTL_DEV_ROOT overrides the default `/dev/scmi` device root used
 * for telemetry character devices.
 *
 * @return Configured SCMI ioctl device root path.
 */
static auto GetScmiIoctlDeviceRootPath() -> std::filesystem::path {
  auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_SCMI_IOCTL_DEV_ROOT);
  if (!env_var_value.empty()) {
    return std::filesystem::path{env_var_value};
  }
  return std::filesystem::path{kDefaultScmiIoctlDeviceRootPath};
}

/** @brief Resolves ASTL_PROCFS_ROOT, falling back to the host procfs root. */
static auto GetProcfsRootPath() -> std::filesystem::path {
  auto env_var_value = astl::GetEnvVar(astl::EnvVar::ASTL_PROCFS_ROOT);
  if (!env_var_value.empty()) {
    return std::filesystem::path{env_var_value};
  }
  return procfs::kDefaultProcfsRootPath;
}

AstlConfiguration::AstlConfiguration(std::filesystem::path const&                scmi_sysfs_path,
                                     std::filesystem::path const&                scmi_ioctl_device_root,
                                     std::filesystem::path const&                procfs_root_path,
                                     std::filesystem::path const&                config_dir_path,
                                     std::optional<std::filesystem::path> const& load_file_path)
    : scmi_sysfs_telemetry_root_path{scmi_sysfs_path},
      scmi_ioctl_device_root_path{scmi_ioctl_device_root},
      procfs_root_path{procfs_root_path},
      config_dir_path{config_dir_path},
      metrics_dir_path{config_dir_path / "metrics"},
      groups_dir_path{config_dir_path / "groups"},
      scmi_specification_dir{config_dir_path / "scmi" / "public"},
      load_file_path{load_file_path} {}

/* Create a AstlConfiguration instance, depending on env variables, and file system paths found. */
[[nodiscard]] auto AstlConfiguration::CreateConfiguration() -> std::expected<AstlConfiguration, astl_status_code> {
  auto scmi_sysfs_path        = GetScmiSysFsTelemetryRootPath();
  auto scmi_ioctl_device_root = GetScmiIoctlDeviceRootPath();
  auto procfs_root_path       = GetProcfsRootPath();
  auto resources              = config::ConfigSource::Create();
  auto collectors             = ParseCollectorSelection();
  if (!resources) {
    return std::unexpected<astl_status_code>(resources.error());
  }
  if (!collectors) {
    return std::unexpected<astl_status_code>(collectors.error());
  }
  auto configuration       = AstlConfiguration(scmi_sysfs_path, scmi_ioctl_device_root, procfs_root_path,
                                               std::filesystem::path{}, std::nullopt);
  configuration.collectors = *collectors;
  configuration.resources  = *resources;
  return configuration;
}

}  // namespace astl
