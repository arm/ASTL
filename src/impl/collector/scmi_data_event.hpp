// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_DATA_EVENT_HPP_
#define SCMI_DATA_EVENT_HPP_

#include <charconv>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#include "astl/astl.h"
#include "common/scmi/scmi_constants.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl {

/*
 * @brief Tracks the state of a configurable data event in SCMI, mainly enabled status and timestamp enabled status
 */
struct ScmiDataEvent {
 public:
  ScmiDataEvent() = delete;
  ScmiDataEvent(ScmiDataEventId event_id, bool originally_enabled, std::optional<bool> timestamp_enabled,
                std::optional<kilohertz> timestamp_rate)
      : id{event_id},
        originally_enabled{originally_enabled},
        timestamp_enabled{timestamp_enabled},
        timestamp_rate{timestamp_rate} {}

  ScmiDataEventId id;              //<!< The unique identifier for this data event
  bool originally_enabled{false};  //!< Whether this data event was originally enabled before collection started
  //!< Whether this data event has timestamp enabled (or nullopt if no file exists)
  std::optional<bool> timestamp_enabled;
  // constant value representing the rate at which this DE's timestamp is updated.
  std::optional<kilohertz> timestamp_rate;
};

/*
 * @brief convert ScmiDataEvent ID and enable status to a string representation we'd expect to see in scmi sysfs.
 */
inline std::string to_string(const ScmiDataEvent& data_event) {
  return std::format("0x{:08X}, originally_enabled: {}, timestamp_enabled: {}", data_event.id,
                     data_event.originally_enabled ? "true" : "false",
                     data_event.timestamp_enabled.value_or(false) ? "true" : "false");
}

constexpr std::string_view kScmiTlmEnableFileName               = "tlm_enable";
constexpr std::string_view kScmiTlmEnableValue                  = "1";
constexpr std::string_view kScmiDataEventEnableFileName         = "enable";
constexpr std::string_view kScmiDataEventEnableValue            = "1";
constexpr std::string_view kScmiDataEventDisableValue           = "0";
constexpr std::string_view kScmiDataEventTstampRateFileName     = "tstamp_rate";
constexpr std::string_view kScmiDataEventTstampEnableFileName   = "tstamp_enable";
constexpr std::string_view kScmiDataEventTstampEnableValue      = "1";
constexpr std::string_view kScmiDataEventTstampDisableValue     = "0";
constexpr std::string_view kScmiDataEventValueFileName          = "value";
constexpr std::string_view kScmiDeImplementationVersionFileName = "de_implementation_version";
constexpr std::string_view kScmiVersion                         = "version";

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
  static std::expected<ScmiDataEventValue, astl_status_code> FromString(std::string_view text) {
    constexpr int base16 = 16;

    decltype(ScmiDataEventValue::value) value{0};
    auto parse_result = std::from_chars(text.data(), text.data() + text.size(), value, base16);
    if (parse_result.ec != std::errc()) {
      ASTL_LOG_ERROR("Failed to parse ScmiDataEventValue from text: {} with error: {}", text,
                     std::make_error_code(parse_result.ec).message());
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);  // Conversion failed
    }
    return ScmiDataEventValue(value);
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
inline std::string to_string(const ScmiDataEventValue& data_event_value) {
  return std::format("{:016X}", data_event_value.value);
}

}  // namespace astl

#endif  // SCMI_DATA_EVENT_HPP_
