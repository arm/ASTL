// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_SCMI_OPERATION_BUILDER_HPP_
#define I_SCMI_OPERATION_BUILDER_HPP_

#include "operation/operation.hpp"
#include "operation/scmi_read_operation.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Builder for creating a single SCMI read operation for a specific Data Event ID.
 *
 */
class ScmiOperationBuilder {
 public:
  explicit ScmiOperationBuilder(ScmiDataEventId data_event_id);

  [[nodiscard]] auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code>;

  [[nodiscard]] auto GetDataEventId() const -> ScmiDataEventId;

 private:
  ScmiDataEventId _data_event_id;
};

/**
 * @brief Builder for creating multiple SCMI read operations for a target based on a map of target names to Data Event
 * IDs.
 */
class ScmiMultiTargetOperationBuilder {
 public:
  explicit ScmiMultiTargetOperationBuilder(ScmiTargetToDataEventIdMap data_event_ids);

  [[nodiscard]] auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code>;

 private:
  ScmiTargetToDataEventIdMap _data_event_ids;
};

static_assert(OperationBuilder<ScmiOperationBuilder>);
static_assert(OperationBuilder<ScmiMultiTargetOperationBuilder>);

}  // namespace astl

#endif  // I_SCMI_OPERATION_BUILDER_HPP_
