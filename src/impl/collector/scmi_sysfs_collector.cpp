// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/scmi_sysfs_collector.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>

#include "astl_logger.hpp"
#include "collector/scmi_data_event.hpp"

namespace astl::scmi_detail {

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
// private helpers
////////////////////////////////////////////////////////////////////////////////

std::expected<fs::path, astl_status_code> GetDataEventDirPath(ScmiDataEventId data_event_id) {
  return fs::path{"des"} / std::format("0x{:08X}", data_event_id);
}

auto ParseScmiTimeStamp(std::string_view text)
    -> std::expected<std::pair<HwClockTicks, std::string_view>, astl_status_code> {
  HwClockTicks tstamp_ticks{0};
  auto         parse_result =
      std::from_chars(text.data(), std::next(text.data(), static_cast<std::ptrdiff_t>(text.size())), tstamp_ticks);
  if (parse_result.ec != std::errc()) {
    ASTL_LOG_ERROR("Failed to parse timestamp from value file text: {} with error: {}", text,
                   std::make_error_code(parse_result.ec).message());
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  auto parsed_length           = static_cast<size_t>(parse_result.ptr - text.data());
  auto remaining_text_to_parse = text.substr(parsed_length);
  // Return raw hardware tick count; MetricManager applies the NativeToMonotonicRawRatio to convert to ns.
  return std::make_pair(tstamp_ticks, remaining_text_to_parse);
}

// Expected format: "<timestamp> <value>"
auto ParseDataEventValueWithTimestamp(std::string_view data_read)
    -> std::expected<std::pair<HwClockTicks, ScmiDataEventValue>, astl_status_code> {
  // timestamp is always provided, even though it may be 0. parse it.
  const auto parse_result = ParseScmiTimeStamp(data_read);
  if (!parse_result) {
    return std::unexpected{parse_result.error()};
  }
  auto [timestamp, remaining_text] = parse_result.value();
  // the timestamp + value are of the form "<timestamp> <value>" OR "<timestamp>: <value",
  // depending on the driver version, so skip past any whitespace or ':' between timestamp and value
  const auto should_skip = [](unsigned char character) { return std::isspace(character) != 0 || character == ':'; };
  // prefer non-pointer auto here since the iterator type of string_view isn't always char* depending on platform
  // NOLINTNEXTLINE(readability-qualified-auto)
  const auto value_it = std::ranges::find_if_not(remaining_text, should_skip);
  if (value_it == std::end(remaining_text)) {
    ASTL_LOG_ERROR("No value found after timestamp in data read: {}", data_read);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  remaining_text = remaining_text.substr(static_cast<size_t>(value_it - remaining_text.begin()));

  // now parse value
  auto value = ScmiDataEventValue::FromString(remaining_text);
  if (!value) {
    return std::unexpected{value.error()};
  }
  return std::make_pair(timestamp, value.value());
}

auto ParseDataEventValue(std::string_view data_read) -> std::expected<ScmiDataEventValue, astl_status_code> {
  constexpr std::string_view delimiters{" \t\n\r\f\v:"};
  const auto                 value_end = data_read.find_last_not_of(delimiters);
  if (value_end == std::string_view::npos) {
    ASTL_LOG_ERROR("No value found in data read: {}", data_read);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  const auto value_delimiter = data_read.find_last_of(delimiters, value_end);
  const auto value_begin     = value_delimiter == std::string_view::npos ? size_t{0} : value_delimiter + 1;
  const auto value_text      = data_read.substr(value_begin, value_end - value_begin + 1);
  return ScmiDataEventValue::FromString(value_text);
}

auto MakeSoftwareClockCorrelation() -> OperationClockCorrelation {
  const auto raw_now = ClockMonotonicRaw::now();
  // Software-clock native ticks are explicitly nanoseconds, matching ClockMonotonicRaw::duration and the 1:1 ratio.
  const auto native_anchor = static_cast<HwClockTicks>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(raw_now.time_since_epoch()).count());
  return OperationClockCorrelation{
      raw_now, native_anchor, NativeToMonotonicRawRatio{1, 1}
  };
}

}  // namespace astl::scmi_detail
