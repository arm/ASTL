// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/collector_builder.hpp"

#include <algorithm>

#include "collector/collector_manager.hpp"
#include "collector/i_collector.hpp"

namespace astl {
auto BuildCollectorManager(const BackendTargets& targets, const AstlConfiguration& configuration,
                           CollectorBackends backends)
    -> std::expected<std::unique_ptr<ICollectorManager>, astl_status_code> {
  if (auto status = ValidateCollectorBackends(backends); status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  std::unordered_map<const ITarget*, std::vector<std::unique_ptr<ICollector>>> collectors;
  for (const auto& target : targets) {
    const auto backend = std::ranges::find(backends, target->GetCollectorType(), &CollectorBackend::type);
    if (backend == backends.end() || backend->create == nullptr) {
      return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
    }
    auto collector = backend->create(*target, configuration);
    if (!collector) {
      return std::unexpected(collector.error());
    }
    if (*collector) {
      collectors[target.get()].push_back(std::move(*collector));
    }
  }
  return std::make_unique<CollectorManager>(std::move(collectors));
}
}  // namespace astl
