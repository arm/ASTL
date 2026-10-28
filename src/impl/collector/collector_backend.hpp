// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTOR_BACKEND_HPP_
#define COLLECTOR_BACKEND_HPP_

#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "astl/astl_errors.h"
#include "common/capabilities.hpp"

namespace astl {
struct AstlConfiguration;
struct ITarget;
struct ICollector;
struct IMetricManager;
using BackendTargets   = std::vector<std::unique_ptr<ITarget>>;
using BackendTargetMap = std::unordered_map<CollectorType, std::vector<const ITarget*>>;

// Stateless backend contract. Resources belong to targets/collectors, not the registry.
// Null discovery marks a known but compiled-out backend; metadata remains available for selection.
struct CollectorBackend {
  CollectorType    type;
  std::string_view name;
  std::expected<BackendTargets, astl_status_code> (*discover)(const AstlConfiguration&);
  std::expected<std::unique_ptr<ICollector>, astl_status_code> (*create)(const ITarget&, const AstlConfiguration&);
  astl_status_code (*register_metrics)(const AstlConfiguration&, const BackendTargetMap&, IMetricManager*);
  astl_status_code (*prepare_targets)(const AstlConfiguration&, const BackendTargets&) = nullptr;
};
using CollectorBackends = std::span<const CollectorBackend>;
auto BuiltinCollectorBackends() -> CollectorBackends;
auto ValidateCollectorBackends(CollectorBackends backends) -> astl_status_code;
}  // namespace astl
#endif
