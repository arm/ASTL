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

#include "collector/scmi_sysfs_collector.hpp"

#include <expected>
#include <filesystem>
#include <format>
#include <unordered_set>
#include <vector>

#include "astl_logger.hpp"
#include "collector/scmi_data_event.hpp"
#include "common/scmi/scmi_read_operation.hpp"
#include "operation.hpp"

namespace astl {

namespace scmi_detail {

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
// private helpers
////////////////////////////////////////////////////////////////////////////////

std::expected<fs::path, astl_status_code> GetDataEventDirPath(ScmiDataEventId data_event_id) {
  if (data_event_id >= kScmiFirstReservedDataEventId) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);  // Reserved data event ID - unsupported in current spec
  }
  return fs::path{"des"} / std::format("0x{:04X}", data_event_id);
}

std::expected<SampleTimestamp, astl_status_code> ParseScmiTimeStamp(std::string const& timestamp_str) {
  try {
    /* SCMI spec says
     * "The selection of a time base is beyond the scope of this specification
     * and should be agreed between the agent and the platform by other standard mechanisms."
     */
    // for now, assume time base is just in unix seconds since epoch,
    // since that appears to be the case used in examples here:
    // https://confluence.arm.com/display/CESW/Linux+Kernel+SCMI+Telemetry+Support+-+v4.0+ALPHA_0+--+WIP
    auto time_since_boot = std::chrono::seconds{std::stoull(timestamp_str)};
    return SampleTimestamp{time_since_boot};
  } catch (const std::invalid_argument&) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);  // Conversion failed
  } catch (const std::out_of_range&) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);  // Value out of range
  }
}

// Expected format: "<timestamp> <value>"
std::expected<std::pair<SampleTimestamp, ScmiDataEventValue>, astl_status_code> ParseDataEventValueWithTimestamp(
    std::string const& data_read) {
  auto space_pos = data_read.find(' ');
  if (space_pos == std::string::npos) {
    ASTL_LOG_ERROR("ParseDataEventValueWithTimestamp: No space found in data_read: {}", data_read);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  // timestamp is provided, parse it
  auto expected_timestamp = ParseScmiTimeStamp(data_read.substr(0, space_pos));
  if (!expected_timestamp) {
    return std::unexpected{expected_timestamp.error()};
  }
  // now parse value
  auto value = ScmiDataEventValue::FromString(data_read.substr(space_pos + 1));
  if (!value) {
    return std::unexpected{value.error()};
  }
  return std::make_pair(expected_timestamp.value(), value.value());
}

// TODO(https://github.com/Arm-Debug/ASTL/issues/92) - potentially disable timestamps depending on chosen optimization

std::unordered_set<ScmiDataEventId> GetUniqueDataEventsIds(CollectionOperations const& operations) {
  // just get a unique set of all data events in all the operations
  std::unordered_set<ScmiDataEventId> all_data_events;
  auto                                insert_unique_event_ids = [&all_data_events](const auto& operations_list) {
    for (const auto& operation : operations_list) {
      if (const auto* scmi_operation = dynamic_cast<ScmiReadOperation const*>(operation.get())) {
        all_data_events.insert(scmi_operation->scmi_data_event_id);
      }
    }
  };
  insert_unique_event_ids(operations.operationsBeforeStart);
  insert_unique_event_ids(operations.operationsAtStart);
  insert_unique_event_ids(operations.operationsOnSample);
  insert_unique_event_ids(operations.operationsAtStop);
  return all_data_events;
}

}  // namespace scmi_detail
}  // namespace astl
