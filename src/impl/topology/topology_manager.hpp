// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TOPOLOGY_MANAGER_HPP_
#define TOPOLOGY_MANAGER_HPP_

#include <memory>
#include <vector>

#include "target.hpp"
#include "topology/i_topology_manager.hpp"

namespace astl {

class TopologyManager : public ITopologyManager {
 public:
  TopologyManager() = delete;

  explicit TopologyManager(std::vector<std::unique_ptr<ITarget>>&&);

  auto GetTargets() const -> const std::vector<std::unique_ptr<ITarget>>& override;

  auto SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets) -> astl_status_code override;

 private:
  std::vector<std::unique_ptr<ITarget>> _targets;
};

}  // namespace astl

#endif  // TOPOLOGY_MANAGER_HPP_
