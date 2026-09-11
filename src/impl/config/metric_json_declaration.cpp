// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/metric_json_declaration.hpp"

#include <limits>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "common/lifecycle_event.hpp"

namespace astl::metrics::spec {

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType> {
  auto collector_type_lower = astl::ToLowerCopy(metric_declaration.collection.protocol);
  if (collector_type_lower == "scmi") {
    return CollectorType::SCMI;
  }
  if (collector_type_lower == "libsensors") {
    return CollectorType::LIBSENSORS;
  }
  if (collector_type_lower == "procfs") {
    return CollectorType::PROCFS;
  }
  return std::nullopt;
}

namespace {

template <typename ValueType>
auto MakeEventValue(uint64_t value, std::string_view name, std::string_view metric_key_name)
    -> std::expected<AstlValue, astl_status_code> {
  if (value > std::numeric_limits<ValueType>::max()) {
    ASTL_LOG_ERROR("Event value for name '{}' in metric {} is out of range", name, metric_key_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return AstlValue{static_cast<ValueType>(value)};
}

auto MakeEventValueForType(uint64_t value, astl_value_type_t value_type, std::string_view name,
                           std::string_view metric_key_name) -> std::expected<AstlValue, astl_status_code> {
  switch (value_type) {
    case ASTL_VALUE_UINT8:
      return MakeEventValue<uint8_t>(value, name, metric_key_name);
    case ASTL_VALUE_UINT16:
      return MakeEventValue<uint16_t>(value, name, metric_key_name);
    case ASTL_VALUE_UINT32:
      return MakeEventValue<uint32_t>(value, name, metric_key_name);
    case ASTL_VALUE_UINT64:
      return MakeEventValue<uint64_t>(value, name, metric_key_name);
    default:
      ASTL_LOG_ERROR("Event metric {} has unsupported output value type {}", metric_key_name,
                     static_cast<int>(value_type));
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

}  // namespace

auto ParseEventValueInfo(std::string_view metric_key_name, const MetricJsonDeclaration& metric_declaration,
                         astl_value_type_t value_type)
    -> std::expected<EventMetricConfig::ValueToInfoMap, astl_status_code> {
  EventMetricConfig::ValueToInfoMap event_value_info;
  if (!EventMetricConfig::IsSupportedValueType(value_type)) {
    ASTL_LOG_ERROR("Event metric {} has unsupported output value type {}", metric_key_name,
                   static_cast<int>(value_type));
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (!metric_declaration.event_values.has_value()) {
    return event_value_info;
  }

  for (const auto& [name, entry] : *metric_declaration.event_values) {
    if (name.empty() || !entry.is_object() || !entry.contains("value") || !entry.contains("description") ||
        !entry["description"].is_string()) {
      ASTL_LOG_ERROR("Invalid event_values entry '{}' for metric {}", name, metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    if (IsReservedLifecycleEventName(name)) {
      ASTL_LOG_ERROR("Event name '{}' is reserved for the ASTL lifecycle metric", name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    uint64_t    unsigned_value{};
    const auto& json_value = entry["value"];
    if (json_value.is_number_unsigned()) {
      unsigned_value = json_value.get<uint64_t>();
    } else if (json_value.is_number_integer()) {
      const auto signed_value = json_value.get<int64_t>();
      if (signed_value < 0) {
        ASTL_LOG_ERROR("Negative event value for name '{}' in metric {} is unsupported", name, metric_key_name);
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      unsigned_value = static_cast<uint64_t>(signed_value);
    } else {
      ASTL_LOG_ERROR("Event value for name '{}' in metric {} must be an unsigned integer", name, metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    auto value = MakeEventValueForType(unsigned_value, value_type, name, metric_key_name);
    if (!value) {
      return std::unexpected(value.error());
    }

    if (!event_value_info
             .emplace(*value, EventMetricConfig::EventValueInfo{name, entry["description"].get<std::string>()})
             .second) {
      ASTL_LOG_ERROR("Duplicate event value for name '{}' in metric {}", name, metric_key_name);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
  }
  return event_value_info;
}

}  // namespace astl::metrics::spec
