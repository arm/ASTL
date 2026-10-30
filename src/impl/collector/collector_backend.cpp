// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/collector_backend.hpp"

#include <array>

#include "astl_logger.hpp"

namespace astl {
// Explicit references keep backend modules linked even when ASTL is a static library.
extern const CollectorBackend kWindowsBackend, kScmiBackend, kLibsensorsBackend, kProcfsBackend, kNumaBackend;
auto                          BuiltinCollectorBackends() -> CollectorBackends {
  static const std::array backends{kWindowsBackend, kScmiBackend, kLibsensorsBackend, kProcfsBackend, kNumaBackend};
  return backends;
}

auto ValidateCollectorBackends(CollectorBackends backends) -> astl_status_code {
  for (size_t i = 0; i < backends.size(); ++i) {
    const auto& backend = backends[i];
    if (backend.name.empty() || backend.type == CollectorType::UNKNOWN || backend.type == CollectorType::ASTL_NATIVE ||
        (backend.discover != nullptr && (backend.create == nullptr || backend.register_metrics == nullptr))) {
      return ASTL_STATUS_BAD_CONFIGURATION;
    }
    for (size_t j = 0; j < i; ++j) {
      if (backend.type == backends[j].type || backend.name == backends[j].name) {
        ASTL_LOG_ERROR("Duplicate collector backend '{}'", backend.name);
        return ASTL_STATUS_BAD_CONFIGURATION;
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}
}  // namespace astl
