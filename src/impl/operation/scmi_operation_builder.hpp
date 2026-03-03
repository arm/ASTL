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
  explicit ScmiOperationBuilder(ScmiDataEventId data_event_id, double value_scale_factor = 1.0);

  [[nodiscard]] auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code>;

  [[nodiscard]] auto GetDataEventId() const -> ScmiDataEventId;

 private:
  ScmiDataEventId _data_event_id;
  double          _value_scale_factor{1.0};  //!< The scale factor for the raw value (based on base10_unit_modifier)
};

/**
 * @brief Builder for creating multiple SCMI read operations for a target based on a map of target names to Data Event
 * IDs.
 */
class ScmiMultiTargetOperationBuilder {
 public:
  explicit ScmiMultiTargetOperationBuilder(ScmiTargetToDataEventIdMap data_event_ids, double value_scale_factor = 1.0);

  [[nodiscard]] auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code>;

 private:
  ScmiTargetToDataEventIdMap _data_event_ids;
  //!< The scale factor for the raw value (determined by base10_unit_modifier
  double _value_scale_factor{1.0};
};

static_assert(OperationBuilder<ScmiOperationBuilder>);
static_assert(OperationBuilder<ScmiMultiTargetOperationBuilder>);

}  // namespace astl

#endif  // I_SCMI_OPERATION_BUILDER_HPP_
