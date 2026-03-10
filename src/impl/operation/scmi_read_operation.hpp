// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file scmi_read_operation.hpp
 * @brief SCMI-specific operation types and data event ID abstractions.
 */
#ifndef SCMI_READ_OPERATION_HPP_
#define SCMI_READ_OPERATION_HPP_

#include <unordered_map>

#include "common/scmi/scmi_constants.hpp"
#include "operation/operation.hpp"

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
  explicit ScmiReadOperation(ScmiDataEventId data_event_id, kilohertz tstamp_rate)
      : scmi_data_event_id{data_event_id}, tstamp_rate{tstamp_rate} {}

  ScmiReadOperation(const ScmiReadOperation&)            = default;
  ScmiReadOperation& operator=(const ScmiReadOperation&) = default;
  ScmiReadOperation(ScmiReadOperation&&)                 = default;
  ScmiReadOperation& operator=(ScmiReadOperation&&)      = default;

  ScmiDataEventId scmi_data_event_id{0};  //!< The SCMI data event ID to be used for this operation
  //!< The timestamp rate in KHz for this data event, used to interpret timestamps if they are enabled
  kilohertz tstamp_rate{1};
};

}  // namespace astl

#endif  // SCMI_READ_OPERATION_HPP_
