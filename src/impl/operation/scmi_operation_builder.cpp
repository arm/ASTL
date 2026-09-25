// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "operation/scmi_operation_builder.hpp"

#include "operation/operation.hpp"
#include "target.hpp"
namespace astl {

ScmiOperationBuilder::ScmiOperationBuilder(ScmiDataEventId data_event_id) : _data_event_id(data_event_id) {}

[[nodiscard]] auto ScmiOperationBuilder::BuildOperations(const ITarget* target) const
    -> std::expected<OperationSequence, astl_status_code> {
  (void)target;
  OperationSequence seq;
  // The collector replaces the default counter rate with a reported timestamp rate during configuration.
  seq.push_back(std::make_unique<ScmiReadOperation>(_data_event_id, kDefaultScmiTimestampRate));
  return seq;
}

[[nodiscard]] auto ScmiOperationBuilder::GetDataEventId() const -> ScmiDataEventId { return _data_event_id; }

ScmiMultiTargetOperationBuilder::ScmiMultiTargetOperationBuilder(ScmiTargetToDataEventIdMap data_event_ids)
    : _data_event_ids(std::move(data_event_ids)) {}

[[nodiscard]] auto ScmiMultiTargetOperationBuilder::BuildOperations(const ITarget* target) const
    -> std::expected<OperationSequence, astl_status_code> {
  const auto stable_target_key = GetStableTargetKey(*target);
  auto       iter              = _data_event_ids.find(stable_target_key);
  if (iter == _data_event_ids.end()) {
    iter = _data_event_ids.find(target->Name());
  }
  if (iter != _data_event_ids.end()) {
    OperationSequence seq;
    seq.reserve(iter->second.size());
    for (const auto& data_event_id : iter->second) {
      // The collector replaces the default counter rate with a reported timestamp rate during configuration.
      seq.push_back(std::make_unique<ScmiReadOperation>(data_event_id, kDefaultScmiTimestampRate));
    }
    return seq;
  }
  ASTL_LOG_ERROR("No Data Event IDs found for target {} (stable key: {})", target->Name(), stable_target_key);
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

}  // namespace astl
