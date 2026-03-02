// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/scmi_sysfs_collector.hpp"

#include <algorithm>
#include <charconv>
#include <expected>
#include <filesystem>
#include <format>
#include <unordered_set>

#include "astl_logger.hpp"
#include "collector/scmi_data_event.hpp"
#include "operation/operation.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl::scmi_detail {

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

auto ParseScmiTimeStamp(std::string_view text, kilohertz tstamp_rate)
    -> std::expected<std::pair<SampleTimestamp, std::string_view>, astl_status_code> {
  if (tstamp_rate == 0) {
    ASTL_LOG_ERROR("Timestamp rate cannot be 0 in ParseScmiTimeStamp");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  uint64_t tstamp_ticks{0};
  auto     parse_result = std::from_chars(text.data(), text.data() + text.size(), tstamp_ticks);
  if (parse_result.ec != std::errc()) {
    ASTL_LOG_ERROR("Failed to parse timestamp from value file text: {} with error: {}", text,
                   std::make_error_code(parse_result.ec).message());
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  constexpr auto micros_per_milli = 1000ULL;
  auto           time_since_boot =
      std::chrono::microseconds{(tstamp_ticks * micros_per_milli) / static_cast<uint64_t>(tstamp_rate)};
  auto parsed_length           = static_cast<size_t>(parse_result.ptr - text.data());
  auto remaining_text_to_parse = text.substr(parsed_length);
  return std::make_pair(SampleTimestamp{time_since_boot}, remaining_text_to_parse);
}

// Expected format: "<timestamp> <value>"
auto ParseDataEventValueWithTimestamp(std::string_view data_read, kilohertz tstamp_rate)
    -> std::expected<std::pair<SampleTimestamp, ScmiDataEventValue>, astl_status_code> {
  // timestamp is always provided, even though it may be 0. parse it.
  const auto parse_result = ParseScmiTimeStamp(data_read, tstamp_rate);
  if (!parse_result) {
    return std::unexpected{parse_result.error()};
  }
  auto [timestamp, remaining_text] = parse_result.value();
  // skip past any whitespace between timestamp and value
  // prefer non-pointer auto here since the iterator type of string_view isn't always char* depending on platform
  // NOLINTNEXTLINE(readability-qualified-auto)
  const auto non_ws_it =
      std::ranges::find_if_not(remaining_text, [](unsigned char maybe_space) { return std::isspace(maybe_space); });
  if (non_ws_it == std::end(remaining_text)) {
    ASTL_LOG_ERROR("No value found after timestamp in data read: {}", data_read);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  remaining_text = remaining_text.substr(static_cast<size_t>(non_ws_it - remaining_text.begin()));

  // now parse value
  auto value = ScmiDataEventValue::FromString(remaining_text);
  if (!value) {
    return std::unexpected{value.error()};
  }
  return std::make_pair(timestamp, value.value());
}

// TODO(https://github.com/Arm-Debug/ASTL/issues/92) - potentially disable timestamps depending on chosen optimization

auto GetUniqueDataEventsIds(CollectionOperations const& operations) -> std::unordered_set<ScmiDataEventId> {
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

/*
 * @brief For each ScmiReadOperation in the given operations, look up its corresponding data event
 *        and copy the timestamp rate.
 *        This is needed so that when we execute a ScmiReadOperation and get a timestamp back,
 *        we know how to interpret it based on the rate at which it increments.
 */
auto UpdateSampleOperationsWithTstampRates(std::vector<ScmiDataEvent> const& data_events,
                                           CollectionOperations const&       operations) -> void {
  auto update_list = [&data_events](const auto& operations_list) {
    for (const auto& operation : operations_list) {
      if (auto* scmi_operation = dynamic_cast<ScmiReadOperation*>(operation.get())) {
        auto data_event_it =
            std::find_if(data_events.begin(), data_events.end(), [&scmi_operation](const ScmiDataEvent& data_event) {
              return data_event.id == scmi_operation->scmi_data_event_id;
            });
        if (data_event_it != data_events.end()) {
          scmi_operation->tstamp_rate = data_event_it->timestamp_rate.value_or(kilohertz{1});
        }
      }
    }
  };

  update_list(operations.operationsBeforeStart);
  update_list(operations.operationsAtStart);
  update_list(operations.operationsOnSample);
  update_list(operations.operationsAtStop);
}

}  // namespace astl::scmi_detail
