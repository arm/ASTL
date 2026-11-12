// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WINDOWS_PDH_SYSTEM_API_HPP_
#define WINDOWS_PDH_SYSTEM_API_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_cxx_compat.hpp"

namespace astl {

struct WindowsPdhMemorySnapshot {
  uint64_t total;
  uint64_t available;
};

// Native PDH identities are retained independently of the portable CPU index.
// Only numeric group,processor instances are admitted, never group totals.
auto SortWindowsPdhProcessorInstances(std::vector<std::string> instances) -> std::vector<std::string>;

class IWindowsPdhSystemApi {
 public:
  IWindowsPdhSystemApi()                                                                                  = default;
  IWindowsPdhSystemApi(const IWindowsPdhSystemApi&)                                                       = delete;
  auto operator=(const IWindowsPdhSystemApi&) -> IWindowsPdhSystemApi&                                    = delete;
  IWindowsPdhSystemApi(IWindowsPdhSystemApi&&)                                                            = delete;
  auto operator=(IWindowsPdhSystemApi&&) -> IWindowsPdhSystemApi&                                         = delete;
  virtual ~IWindowsPdhSystemApi()                                                                         = default;
  virtual auto DiscoverProcessors() -> astl::compat::expected<std::vector<std::string>, astl_status_code> = 0;
  virtual auto ConfigureCpu(const std::vector<std::string>& instances) -> astl_status_code                = 0;
  virtual auto ResetCpu() -> astl_status_code                                                             = 0;
  // Missing optional values mean warm-up or an invalid sample, never 0% utilization.
  virtual auto ReadCpu()
      -> astl::compat::expected<std::unordered_map<std::string, astl::compat::optional<double>>, astl_status_code> = 0;
  virtual auto ReadMemory() -> astl::compat::expected<WindowsPdhMemorySnapshot, astl_status_code>                  = 0;
};

#if defined(ASTL_INCLUDE_WINDOWS_PDH)
auto MakeWindowsPdhSystemApi() -> std::unique_ptr<IWindowsPdhSystemApi>;
#endif

}  // namespace astl
#endif
