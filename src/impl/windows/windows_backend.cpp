// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/collector_backend.hpp"
#include "collector/i_collector.hpp"
#include "config/astl_configuration.hpp"
#if defined(ASTL_INCLUDE_WINDOWS)
#  include "windows/windows_metric_builder.hpp"
#  include "windows/windows_system_collector.hpp"
#endif
namespace astl {
namespace {
#if defined(ASTL_INCLUDE_WINDOWS)
auto Discover(const AstlConfiguration& /*configuration*/) -> std::expected<BackendTargets, astl_status_code> {
  auto api        = MakeWindowsSystemApi();
  auto processors = api->DiscoverProcessors();
  if (!processors) {
    return std::unexpected(processors.error());
  }
  const auto memory = api->ReadMemory();
  if (!memory) {
    return std::unexpected(memory.error());
  }
  BackendTargets targets;
  targets.push_back(std::make_unique<WindowsSystemTarget>(std::move(*processors)));
  return targets;
}
#endif
auto BuildWindowsCollector(const ITarget& /*target*/, const AstlConfiguration& config)
    -> std::expected<std::unique_ptr<ICollector>, astl_status_code> {
  if (config.load_file_path) {
    return nullptr;
  }
#if defined(ASTL_INCLUDE_WINDOWS)
  return std::make_unique<WindowsSystemCollector>(MakeWindowsSystemApi());
#else
  return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
#endif
}
}  // namespace
extern const CollectorBackend kWindowsBackend{CollectorType::WINDOWS, "windows",
#if defined(ASTL_INCLUDE_WINDOWS)
                                              Discover, BuildWindowsCollector, RegisterWindowsMetrics
#else
                                              nullptr, BuildWindowsCollector, nullptr
#endif
};
}  // namespace astl
