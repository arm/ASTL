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

/**
 * @file scmi_read_operation.hpp
 * @brief SCMI-specific operation types and data event ID abstractions.
 */
#ifndef SCMI_READ_OPERATION_HPP_
#define SCMI_READ_OPERATION_HPP_

#include <unordered_map>

#include "common/operation.hpp"

namespace astl {

// Type alias for Data Event Identifiers
using ScmiDataEventId                                   = uint32_t;
constexpr ScmiDataEventId kScmiFirstReservedDataEventId = 0x10000;

// maps a target name to a data event ID for a DE
using ScmiTargetToDataEventIdMap = std::unordered_map<std::string, std::vector<ScmiDataEventId>>;

/*
 * @brief A specialization of Operation that represents a read or write through SCMI
 */
struct ScmiReadOperation : public Operation {
 public:
  ~ScmiReadOperation() override = default;

  // no reasonable default constructor
  ScmiReadOperation() = delete;

  /*
   * @brief Constructor for read operations
   */
  explicit ScmiReadOperation(ScmiDataEventId data_event_id) : scmi_data_event_id{data_event_id} {}

  ScmiReadOperation(const ScmiReadOperation&)            = default;
  ScmiReadOperation& operator=(const ScmiReadOperation&) = default;
  ScmiReadOperation(ScmiReadOperation&&)                 = default;
  ScmiReadOperation& operator=(ScmiReadOperation&&)      = default;

  ScmiDataEventId scmi_data_event_id = 0;  //!< The SCMI data event ID to be used for this operation
};

}  // namespace astl

#endif  // SCMI_READ_OPERATION_HPP_