// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "topology/topology_manager.hpp"

#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "astl/astl_errors.h"

using json = nlohmann::json;

namespace astl {

TopologyManager::TopologyManager(std::vector<std::unique_ptr<ITarget>>&& targets) : _targets{std::move(targets)} {}

auto TopologyManager::GetTargets() const -> const std::vector<std::unique_ptr<ITarget>>& { return _targets; }

auto TopologyManager::SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code {
  _targets = std::move(new_targets);
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
