/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "serdes/protobuf_serdes.hpp"
#include "serdes/targets.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.
#include "topology/topology_manager.hpp"

namespace astl::ProtobufSerDes {

static auto SerializeTarget(const ITarget& target, astl::protobuf::Target* proto_target) -> astl_status_code {
  astl_target_properties_t props{};
  if (auto status = target.GetProperties(&props); status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to get target properties for target {}", target.Name());
    return status;
  }

  proto_target->set_name(target.Name());
  if (props._description != nullptr) {
    proto_target->set_description(props._description);
  }
  proto_target->set_collector_type(static_cast<astl::protobuf::CollectorType>(target.GetCollectorType()));

  // Parent currently unused -> skip _parent_handle
  if (props._uuid != nullptr) {
    proto_target->set_uuid(props._uuid);
  }

  return ASTL_STATUS_SUCCESS;
}

auto Serialize(const ITopologyManager& topology_manager, std::ostream& output_stream) -> astl_status_code {
  const auto& targets = topology_manager.GetTargets();
  if (targets.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    ASTL_LOG_ERROR("Too many targets to serialize: {}", targets.size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  astl::protobuf::TargetList proto_list;
  proto_list.mutable_targets()->Reserve(static_cast<int>(targets.size()));

  for (const auto& target_uptr : targets) {
    if (!target_uptr) {
      continue;
    }
    auto* proto_target = proto_list.add_targets();
    if (auto status = SerializeTarget(*target_uptr, proto_target); status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to serialize target {}", target_uptr->Name());
      return status;
    }
  }

  if (!proto_list.SerializeToOstream(&output_stream)) {
    ASTL_LOG_ERROR("Failed to serialize TargetList to output stream");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

template <>
auto Deserialize<std::unique_ptr<ITopologyManager>>(std::istream& input_stream)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code> {
  astl::protobuf::TargetList proto_list;
  if (!proto_list.ParseFromIstream(&input_stream)) {
    ASTL_LOG_ERROR("Failed to parse TargetList from input stream");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::vector<std::unique_ptr<ITarget>> targets;
  targets.reserve(static_cast<size_t>(proto_list.targets_size()));

  for (const auto& proto_target : proto_list.targets()) {
    const std::string& name = proto_target.name();
    if (name.empty()) {
      ASTL_LOG_ERROR("Target with empty name found in protobuf TargetList");
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    const std::string description = proto_target.description();
    const auto        collector   = static_cast<CollectorType>(proto_target.collector_type());

    std::optional<std::string> uuid{std::nullopt};
    if (!proto_target.uuid().empty()) {
      uuid = proto_target.uuid();
    }

    auto target = std::make_unique<Target>(name, description, collector, nullptr, uuid);

    targets.push_back(std::move(target));
  }

  auto topology_manager = std::make_unique<TopologyManager>(std::move(targets));
  return topology_manager;
}

}  // namespace astl::ProtobufSerDes
