// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WINDOWS_SYSTEM_API_HPP_
#define WINDOWS_SYSTEM_API_HPP_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl_errors.h"

namespace astl {

struct WindowsMemorySnapshot {
  uint64_t total;
  uint64_t available;
};

// Native PDH identities are retained independently of the portable CPU index.
// Only numeric group,processor instances are admitted, never group totals.
auto SortWindowsProcessorInstances(std::vector<std::string> instances) -> std::vector<std::string>;

class IWindowsSystemApi {
 public:
  IWindowsSystemApi()                                                                            = default;
  IWindowsSystemApi(const IWindowsSystemApi&)                                                    = delete;
  auto operator=(const IWindowsSystemApi&) -> IWindowsSystemApi&                                 = delete;
  IWindowsSystemApi(IWindowsSystemApi&&)                                                         = delete;
  auto operator=(IWindowsSystemApi&&) -> IWindowsSystemApi&                                      = delete;
  virtual ~IWindowsSystemApi()                                                                   = default;
  virtual auto DiscoverProcessors() -> std::expected<std::vector<std::string>, astl_status_code> = 0;
  virtual auto ConfigureCpu(const std::vector<std::string>& instances) -> astl_status_code       = 0;
  virtual auto ResetCpu() -> astl_status_code                                                    = 0;
  // Missing optional values mean warm-up or an invalid sample, never 0% utilization.
  virtual auto ReadCpu() -> std::expected<std::unordered_map<std::string, std::optional<double>>, astl_status_code> = 0;
  virtual auto ReadMemory() -> std::expected<WindowsMemorySnapshot, astl_status_code>                               = 0;
};

#if defined(ASTL_INCLUDE_WINDOWS)
auto MakeWindowsSystemApi() -> std::unique_ptr<IWindowsSystemApi>;
#endif

}  // namespace astl
#endif
