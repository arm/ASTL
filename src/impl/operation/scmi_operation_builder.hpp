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
