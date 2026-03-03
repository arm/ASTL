// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "operation/scmi_operation_builder.hpp"

#include "operation/operation.hpp"
#include "target.hpp"
namespace astl {

ScmiOperationBuilder::ScmiOperationBuilder(ScmiDataEventId data_event_id, double value_scale_factor)
    : _data_event_id(data_event_id), _value_scale_factor(value_scale_factor) {}

[[nodiscard]] auto ScmiOperationBuilder::BuildOperations(const ITarget* target) const
    -> std::expected<OperationSequence, astl_status_code> {
  (void)target;
  OperationSequence seq;
  // default to 1 KHz if not specified, meaning timestamps are in milliseconds
  // When sample collection is configured, the scmi collector should read the tstamp_rate files and _update_
  // the tstamp_rate_khz value in the ScmiReadOperations so we can interpret the timestamps correctly when sampling
  const kilohertz default_tstamp_rate{1};
  seq.push_back(std::make_unique<ScmiReadOperation>(_data_event_id, default_tstamp_rate, _value_scale_factor));
  return seq;
}

[[nodiscard]] auto ScmiOperationBuilder::GetDataEventId() const -> ScmiDataEventId { return _data_event_id; }

ScmiMultiTargetOperationBuilder::ScmiMultiTargetOperationBuilder(ScmiTargetToDataEventIdMap data_event_ids,
                                                                 double                     value_scale_factor)
    : _data_event_ids(std::move(data_event_ids)), _value_scale_factor(value_scale_factor) {}

[[nodiscard]] auto ScmiMultiTargetOperationBuilder::BuildOperations(const ITarget* target) const
    -> std::expected<OperationSequence, astl_status_code> {
  if (const auto iter = _data_event_ids.find(target->Name()); iter != _data_event_ids.end()) {
    OperationSequence seq;
    seq.reserve(iter->second.size());
    for (const auto& data_event_id : iter->second) {
      // default to 1 KHz if not specified, meaning timestamps are in milliseconds
      // When sample collection is configured, the scmi collector should read the tstamp_rate files and _update_
      // the tstamp_rate_khz value in the ScmiReadOperations so we can interpret the timestamps correctly when sampling
      const kilohertz default_tstamp_rate{1};
      seq.push_back(std::make_unique<ScmiReadOperation>(data_event_id, default_tstamp_rate, _value_scale_factor));
    }
    return seq;
  }
  ASTL_LOG_ERROR("No Data Event IDs found for target {}", target->Name());
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

}  // namespace astl
