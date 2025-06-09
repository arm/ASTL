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

#ifndef SCMI_DATA_EVENT_HPP_
#define SCMI_DATA_EVENT_HPP_

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#include "astl/astl.h"
#include "common/scmi/scmi_read_operation.hpp"

namespace astl {

/*
 * @brief Tracks the state of a configurable data event in SCMI, mainly enabled status and timestamp enabled status
 */
struct ScmiDataEvent {
 public:
  ScmiDataEvent() = delete;
  ScmiDataEvent(ScmiDataEventId event_id, bool originally_enabled, bool timestamp_enabled)
      : id{event_id}, originally_enabled{originally_enabled}, timestamp_enabled{timestamp_enabled} {}

  ScmiDataEventId id;               //<!< The unique identifier for this data event
  bool originally_enabled = false;  //!< Whether this data event was originally enabled before collection started
  bool timestamp_enabled  = false;  //!< Whether this data event has timestamp enabled
};

/*
 * @brief convert ScmiDataEvent ID and enable status to a string representation we'd expect to see in scmi sysfs.
 */
inline std::string to_string(const astl::ScmiDataEvent& data_event) {
  return std::format("0x{:04X}, originally_enabled: {}, timestamp_enabled: {}", data_event.id,
                     data_event.originally_enabled ? "true" : "false", data_event.timestamp_enabled ? "true" : "false");
}

constexpr std::string_view kScmiTlmEnableFileName                   = "tlm_enable";
constexpr std::string_view kScmiTlmEnableValue                      = "1";
constexpr std::string_view kScmiDataEventEnableFileName             = "enable";
constexpr std::string_view kScmiDataEventEnableValue                = "1";
constexpr std::string_view kScmiDataEventDisableValue               = "0";
constexpr std::string_view kScmiDataEventTstampEnableFileName       = "tstamp_enable";
constexpr std::string_view kScmiDataEventTstampEnableValue          = "1";
constexpr std::string_view kScmiDataEventTstampDisableValue         = "0";
constexpr std::string_view kScmiDataEventValueFileName              = "value";
constexpr std::string_view kScmiInfoDirName                         = "info";
constexpr std::string_view kScmiInfoDeImplementationVersionFileName = "de_implementation_version";
constexpr std::string_view kScmiInfoVersion                         = "version";

/*
 * @brief The data read from an SCMI sysfs data event's 'value' file.
 */
struct ScmiDataEventValue {
 public:
  ScmiDataEventValue() = default;
  explicit ScmiDataEventValue(uint64_t value) : value{value} {}

  /* @brief parses the given string as a 64-bit hexadecimal number
   *  for now, assume this translates to a uint64_t astl_value_t variant
   * @param str - a substring of a SCMI DataEvent "value" file represented as a hexadecimal number that fits in 64 bits
   * @return Usually a ScmiDataEventValue with the parsed result. Maybe a ASTL_STATUS_BAD_ARGUMENT on a parse error
   */
  static std::expected<ScmiDataEventValue, astl_status_code> FromString(const std::string& str) {
    constexpr int base16 = 16;
    try {
      return ScmiDataEventValue(std::stoull(str, nullptr, base16));
    } catch (const std::invalid_argument&) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);  // Conversion failed
    } catch (const std::out_of_range&) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);  // Value out of range
    }
  }

  /*
   * @brief convert this Scmi value to a astl_value_t.
   * for now, assume this translates to a uint64_t astl_value_t variant in all cases.
   */
  astl_value_t AsAstlValue() const { return astl_value_t{.ui64 = value}; }

  uint64_t value = 0;
};

/*
 * @brief convert ScmiDataEventValue to a string representation we'd expect to see in scmi sysfs.
 */
inline std::string to_string(const astl::ScmiDataEventValue& data_event_value) {
  return std::format("{:016X}", data_event_value.value);
}

}  // namespace astl

#endif  // SCMI_DATA_EVENT_HPP_