// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "libsensors/libsensors_metric_builder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "astl_internal_status.hpp"
#include "astl_utils.hpp"
#include "config/astl_configuration.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_json_declaration.hpp"
#include "metric/formula_builder.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_api.hpp"
#  include "libsensors/libsensors_target.hpp"
#endif

namespace astl {

#if defined(ASTL_INCLUDE_LIBSENSORS)

struct DiscoveredSensorMetric {
  const sensors_chip_name*   chip;
  const sensors_feature*     feature;
  const sensors_subfeature*  subfeature;
  std::string                chip_name;
  std::string                label;
  std::string                register_name;
  astl_units_t               units;
  astl_metric_identifier_t   identifier;
  std::optional<std::string> description;
};

struct LibsensorsMetricRegistrationDetails {
  std::string              name;
  std::string              description;
  astl_units_t             units;
  astl_metric_identifier_t identifier;
  astl_metric_type_t       metric_type;
  std::vector<std::string> metric_groups;
  AnyFormula               formula;
};

struct LibsensorsTargetContext {
  const LibsensorsTarget*                          target;
  std::optional<metrics::spec::MetricsDeclaration> declarations;
  std::string                                      metric_name_prefix;
  enum class DeclarationMatchKind {
    NONE,
    EXACT,
    FALLBACK,
  } declaration_match_kind;
};

struct LoadedMetricDeclarations {
  std::optional<metrics::spec::MetricsDeclaration> declarations;
  LibsensorsTargetContext::DeclarationMatchKind    match_kind{LibsensorsTargetContext::DeclarationMatchKind::NONE};
};

enum class DerivedUnitsPolicy {
  INHERIT,
  NONE,
  SECONDS,
  COUNT,
};

enum class DerivedIdentifierPolicy {
  INHERIT,
  STATUS,
  THERMAL_LIMIT,
};

struct DerivedSubfeatureDescriptor {
  sensors_subfeature_type subtype;
  const char*             register_suffix;
  const char*             description_prefix;
  DerivedUnitsPolicy      units_policy;
  DerivedIdentifierPolicy identifier_policy;
};

struct FeatureDescriptor {
  std::array<sensors_subfeature_type, 2>       primary_subfeatures;
  size_t                                       primary_subfeature_count;
  astl_units_t                                 units;
  astl_metric_identifier_t                     identifier;
  std::span<const DerivedSubfeatureDescriptor> derived_subfeatures;
};

constexpr std::array<DerivedSubfeatureDescriptor, 13> kInDerivedSubfeatures{
    {
     {SENSORS_SUBFEATURE_IN_MIN, "min", "Minimum reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_MAX, "max", "Maximum reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_LCRIT, "lcrit", "Low critical threshold for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_CRIT, "crit", "Critical threshold for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_AVERAGE, "average", "Average reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_LOWEST, "lowest", "Lowest recorded reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_HIGHEST, "highest", "Highest recorded reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_IN_ALARM, "alarm", "Alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_IN_MIN_ALARM, "min_alarm", "Minimum threshold alarm for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_IN_MAX_ALARM, "max_alarm", "Maximum threshold alarm for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_IN_BEEP, "beep", "Beep status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_IN_LCRIT_ALARM, "lcrit_alarm", "Low critical threshold alarm for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_IN_CRIT_ALARM, "crit_alarm", "Critical threshold alarm for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     }
};

constexpr std::array<DerivedSubfeatureDescriptor, 9> kFanDerivedSubfeatures{
    {
     {SENSORS_SUBFEATURE_FAN_MIN, "min", "Minimum fan speed for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_FAN_MAX, "max", "Maximum fan speed for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_FAN_ALARM, "alarm", "Fan alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_FAN_FAULT, "fault", "Fan fault status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_FAN_DIV, "div", "Fan divisor for ", DerivedUnitsPolicy::COUNT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_FAN_BEEP, "beep", "Fan beep status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_FAN_PULSES, "pulses", "Fan pulses per revolution for ", DerivedUnitsPolicy::COUNT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_FAN_MIN_ALARM, "min_alarm", "Minimum fan speed alarm for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_FAN_MAX_ALARM, "max_alarm", "Maximum fan speed alarm for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     }
};

constexpr std::array<DerivedSubfeatureDescriptor, 22> kTempDerivedSubfeatures{
    {
     {SENSORS_SUBFEATURE_TEMP_MIN, "thermal_limit_low", "Low thermal limit for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::THERMAL_LIMIT},
     {SENSORS_SUBFEATURE_TEMP_MAX, "thermal_limit_high", "High thermal limit for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::THERMAL_LIMIT},
     {SENSORS_SUBFEATURE_TEMP_CRIT, "thermal_limit_critical", "Critical thermal limit for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::THERMAL_LIMIT},
     {SENSORS_SUBFEATURE_TEMP_EMERGENCY, "thermal_limit_emergency", "Emergency thermal limit for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::THERMAL_LIMIT},
     {SENSORS_SUBFEATURE_TEMP_MAX_HYST, "thermal_limit_high_hysteresis", "High thermal hysteresis for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_CRIT_HYST, "thermal_limit_critical_hysteresis", "Critical thermal hysteresis for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_LCRIT, "thermal_limit_critical_low", "Low critical thermal limit for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_EMERGENCY_HYST, "thermal_limit_emergency_hysteresis",
         "Emergency thermal hysteresis for ", DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_LOWEST, "lowest", "Lowest recorded temperature for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_HIGHEST, "highest", "Highest recorded temperature for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_MIN_HYST, "thermal_limit_low_hysteresis", "Low thermal hysteresis for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_LCRIT_HYST, "thermal_limit_critical_low_hysteresis",
         "Low critical thermal hysteresis for ", DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_ALARM, "alarm", "Thermal alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_MAX_ALARM, "max_alarm", "High thermal alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_MIN_ALARM, "min_alarm", "Low thermal alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_CRIT_ALARM, "crit_alarm", "Critical thermal alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_FAULT, "fault", "Thermal fault status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_TYPE, "sensor_type", "Temperature sensor type for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_OFFSET, "offset", "Temperature offset for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_TEMP_BEEP, "beep", "Thermal beep status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_EMERGENCY_ALARM, "emergency_alarm", "Emergency thermal alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_TEMP_LCRIT_ALARM, "lcrit_alarm", "Low critical thermal alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     }
};

constexpr std::array<DerivedSubfeatureDescriptor, 18> kPowerDerivedSubfeatures{
    {
     {SENSORS_SUBFEATURE_POWER_AVERAGE, "average", "Average power reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_AVERAGE_HIGHEST, "average_highest", "Highest average power reading for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_AVERAGE_LOWEST, "average_lowest", "Lowest average power reading for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_INPUT_HIGHEST, "input_highest", "Highest instantaneous power reading for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_INPUT_LOWEST, "input_lowest", "Lowest instantaneous power reading for ",
         DerivedUnitsPolicy::INHERIT, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_CAP, "cap", "Power cap for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_CAP_HYST, "cap_hyst", "Power cap hysteresis for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_MAX, "max", "Maximum power limit for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_CRIT, "crit", "Critical power limit for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_MIN, "min", "Minimum power limit for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_LCRIT, "lcrit", "Low critical power limit for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_AVERAGE_INTERVAL, "average_interval", "Power averaging interval for ",
         DerivedUnitsPolicy::SECONDS, DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_POWER_ALARM, "alarm", "Power alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_POWER_CAP_ALARM, "cap_alarm", "Power cap alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_POWER_MAX_ALARM, "max_alarm", "Maximum power alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_POWER_CRIT_ALARM, "crit_alarm", "Critical power alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_POWER_MIN_ALARM, "min_alarm", "Minimum power alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_POWER_LCRIT_ALARM, "lcrit_alarm", "Low critical power alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     }
};

constexpr std::array<DerivedSubfeatureDescriptor, 13> kCurrDerivedSubfeatures{
    {
     {SENSORS_SUBFEATURE_CURR_MIN, "min", "Minimum current reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_MAX, "max", "Maximum current reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_LCRIT, "lcrit", "Low critical current threshold for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_CRIT, "crit", "Critical current threshold for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_AVERAGE, "average", "Average current reading for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_LOWEST, "lowest", "Lowest recorded current for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_HIGHEST, "highest", "Highest recorded current for ", DerivedUnitsPolicy::INHERIT,
         DerivedIdentifierPolicy::INHERIT},
     {SENSORS_SUBFEATURE_CURR_ALARM, "alarm", "Current alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_CURR_MIN_ALARM, "min_alarm", "Minimum current alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_CURR_MAX_ALARM, "max_alarm", "Maximum current alarm status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_CURR_BEEP, "beep", "Current beep status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_CURR_LCRIT_ALARM, "lcrit_alarm", "Low critical current alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     {SENSORS_SUBFEATURE_CURR_CRIT_ALARM, "crit_alarm", "Critical current alarm status for ",
         DerivedUnitsPolicy::NONE, DerivedIdentifierPolicy::STATUS},
     }
};

constexpr std::array<DerivedSubfeatureDescriptor, 1> kIntrusionDerivedSubfeatures{
    {
     {SENSORS_SUBFEATURE_INTRUSION_BEEP, "beep", "Intrusion beep status for ", DerivedUnitsPolicy::NONE,
         DerivedIdentifierPolicy::STATUS},
     }
};

static auto GetFeatureDescriptor(sensors_feature_type feature_type) -> std::optional<FeatureDescriptor> {
  switch (feature_type) {
    case SENSORS_FEATURE_IN:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_IN_INPUT, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_VOLTS,
          ASTL_METRIC_IDENTIFIER_VOLTAGE,
          kInDerivedSubfeatures
      };
    case SENSORS_FEATURE_FAN:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_FAN_INPUT, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_RPM,
          ASTL_METRIC_IDENTIFIER_FAN_SPEED,
          kFanDerivedSubfeatures
      };
    case SENSORS_FEATURE_TEMP:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_TEMP_INPUT, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_CELSIUS,
          ASTL_METRIC_IDENTIFIER_TEMPERATURE,
          kTempDerivedSubfeatures
      };
    case SENSORS_FEATURE_POWER:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_POWER_INPUT, SENSORS_SUBFEATURE_POWER_AVERAGE},
          2,
          ASTL_UNITS_WATTS,
          ASTL_METRIC_IDENTIFIER_POWER,
          kPowerDerivedSubfeatures
      };
    case SENSORS_FEATURE_ENERGY:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_ENERGY_INPUT, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_JOULES,
          ASTL_METRIC_IDENTIFIER_ENERGY,
          {}
      };
    case SENSORS_FEATURE_CURR:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_CURR_INPUT, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_AMPS,
          ASTL_METRIC_IDENTIFIER_CURRENT,
          kCurrDerivedSubfeatures
      };
    case SENSORS_FEATURE_HUMIDITY:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_HUMIDITY_INPUT, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_PERCENT,
          ASTL_METRIC_IDENTIFIER_HUMIDITY,
          {}
      };
    case SENSORS_FEATURE_VID:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_VID, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_VOLTS,
          ASTL_METRIC_IDENTIFIER_VOLTAGE,
          {}
      };
    case SENSORS_FEATURE_INTRUSION:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_INTRUSION_ALARM, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_NONE,
          ASTL_METRIC_IDENTIFIER_STATUS,
          kIntrusionDerivedSubfeatures
      };
    case SENSORS_FEATURE_BEEP_ENABLE:
      return FeatureDescriptor{
          {SENSORS_SUBFEATURE_BEEP_ENABLE, SENSORS_SUBFEATURE_UNKNOWN},
          1,
          ASTL_UNITS_NONE,
          ASTL_METRIC_IDENTIFIER_STATUS,
          {}
      };
    default:
      return std::nullopt;
  }
}

static auto ResolveUnitsFromPolicy(astl_units_t base_units, DerivedUnitsPolicy policy) -> astl_units_t {
  switch (policy) {
    case DerivedUnitsPolicy::INHERIT:
      return base_units;
    case DerivedUnitsPolicy::NONE:
      return ASTL_UNITS_NONE;
    case DerivedUnitsPolicy::SECONDS:
      return ASTL_UNITS_SECONDS;
    case DerivedUnitsPolicy::COUNT:
      return ASTL_UNITS_COUNT;
  }
  return ASTL_UNITS_UNKNOWN;
}

static auto ResolveIdentifierFromPolicy(astl_metric_identifier_t base_identifier, DerivedIdentifierPolicy policy)
    -> astl_metric_identifier_t {
  switch (policy) {
    case DerivedIdentifierPolicy::INHERIT:
      return base_identifier;
    case DerivedIdentifierPolicy::STATUS:
      return ASTL_METRIC_IDENTIFIER_STATUS;
    case DerivedIdentifierPolicy::THERMAL_LIMIT:
      return ASTL_METRIC_IDENTIFIER_THERMAL_LIMIT;
  }
  return ASTL_METRIC_IDENTIFIER_UNKNOWN;
}

static auto DeclaredRegisterName(std::string_view                            declared_metric_name,
                                 const metrics::spec::MetricJsonDeclaration& metric_declaration) -> std::string_view {
  if (!metric_declaration.collection.register_name.empty()) {
    return metric_declaration.collection.register_name;
  }
  return declared_metric_name;
}

static auto SensorMatchesDeclaredRegister(const DiscoveredSensorMetric& sensor, std::string_view declared_register_name)
    -> bool {
  return declared_register_name == sensor.label || declared_register_name == sensor.register_name;
}

static auto RecordObservedSensorNames(std::unordered_set<std::string>& observed_register_names, std::string_view label,
                                      std::string_view register_name) -> void {
  if (!label.empty()) {
    observed_register_names.emplace(label);
  }
  if (!register_name.empty()) {
    observed_register_names.emplace(register_name);
  }
}

static auto MergeMetricsDeclarationJson(nlohmann::json base_json, const nlohmann::json& overlay_json)
    -> nlohmann::json {
  auto merged_json = std::move(base_json);
  for (const auto& [key, value] : overlay_json.items()) {
    if (key == "extends") {
      continue;
    }
    if (key == "document" && value.is_object()) {
      auto& merged_document = merged_json["document"];
      if (!merged_document.is_object()) {
        merged_document = nlohmann::json::object();
      }
      for (const auto& [document_key, document_value] : value.items()) {
        merged_document[document_key] = document_value;
      }
      continue;
    }
    if (key == "metrics" && value.is_object()) {
      auto& merged_metrics = merged_json["metrics"];
      if (!merged_metrics.is_object()) {
        merged_metrics = nlohmann::json::object();
      }
      for (const auto& [metric_key, metric_value] : value.items()) {
        merged_metrics[metric_key] = metric_value;
      }
      continue;
    }
    merged_json[key] = value;
  }
  return merged_json;
}

static auto LoadMergedMetricsDeclarationJson(const std::filesystem::path&     json_file_path,
                                             std::unordered_set<std::string>& active_paths)
    -> std::expected<nlohmann::json, astl_status_code>;

class ActiveDeclarationPathScope {
 public:
  ActiveDeclarationPathScope(std::unordered_set<std::string>& active_paths, std::string path_key)
      : _active_paths(active_paths), _path_key(std::move(path_key)) {}
  ActiveDeclarationPathScope(const ActiveDeclarationPathScope&)                        = delete;
  auto operator=(const ActiveDeclarationPathScope&) -> ActiveDeclarationPathScope&     = delete;
  ActiveDeclarationPathScope(ActiveDeclarationPathScope&&) noexcept                    = delete;
  auto operator=(ActiveDeclarationPathScope&&) noexcept -> ActiveDeclarationPathScope& = delete;
  ~ActiveDeclarationPathScope() { _active_paths.erase(_path_key); }

 private:
  std::unordered_set<std::string>& _active_paths;
  std::string                      _path_key;
};

static auto MergeParentDeclarationIfPresent(const std::filesystem::path& normalized_path, nlohmann::json& merged_json,
                                            std::unordered_set<std::string>& active_paths)
    -> std::expected<void, astl_status_code> {
  if (!merged_json.contains("extends")) {
    return {};
  }
  if (!merged_json["extends"].is_string()) {
    ASTL_LOG_ERROR("libsensors metrics declaration {} uses a non-string 'extends' field", normalized_path.string());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  auto parent_path = std::filesystem::path{merged_json["extends"].get<std::string>()};
  if (parent_path.is_relative()) {
    parent_path = normalized_path.parent_path() / parent_path;
  }

  auto parent_json_or_error = LoadMergedMetricsDeclarationJson(parent_path, active_paths);
  if (!parent_json_or_error.has_value()) {
    return std::unexpected(parent_json_or_error.error());
  }
  merged_json = MergeMetricsDeclarationJson(std::move(parent_json_or_error.value()), merged_json);
  return {};
}

static auto LoadMergedMetricsDeclarationJson(const std::filesystem::path&     json_file_path,
                                             std::unordered_set<std::string>& active_paths)
    -> std::expected<nlohmann::json, astl_status_code> {
  std::error_code ec;
  const auto      normalized_path = std::filesystem::weakly_canonical(json_file_path, ec);
  if (ec) {
    ASTL_LOG_ERROR("Failed to canonicalize libsensors metrics declaration path {}: {}", json_file_path.string(),
                   ec.message());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  const auto active_key = normalized_path.string();
  if (!active_paths.insert(active_key).second) {
    ASTL_LOG_ERROR("Detected cyclic libsensors metrics declaration inheritance involving {}", active_key);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  const ActiveDeclarationPathScope active_path_scope{active_paths, active_key};

  auto loaded_json_or_error = config::LoadJsonFile(normalized_path);
  if (!loaded_json_or_error.has_value()) {
    return std::unexpected(loaded_json_or_error.error());
  }

  auto merged_json         = std::move(loaded_json_or_error.value());
  auto merge_parent_status = MergeParentDeclarationIfPresent(normalized_path, merged_json, active_paths);
  if (!merge_parent_status.has_value()) {
    return std::unexpected(merge_parent_status.error());
  }
  return merged_json;
}

struct DeclaredDerivedMetric {
  std::string_view label_suffix;
  std::string_view description_prefix;
};

struct DeclaredDerivedMetricAlias {
  std::string_view      alias;
  DeclaredDerivedMetric metric;
};

static auto FindDeclaredDerivedMetric(std::string_view normalized_metric_name) -> std::optional<DeclaredDerivedMetric> {
  static constexpr auto k_declared_derived_metric_aliases = std::to_array<DeclaredDerivedMetricAlias>({
      {"low",                     {"thermal_limit_low", "Low thermal limit for "}                                  },
      {"min",                     {"min", "Minimum reading for "}                                                  },
      {"high",                    {"thermal_limit_high", "High thermal limit for "}                                },
      {"max",                     {"max", "Maximum reading for "}                                                  },
      {"crit",                    {"thermal_limit_critical", "Critical thermal limit for "}                        },
      {"critical",                {"thermal_limit_critical", "Critical thermal limit for "}                        },
      {"emerg",                   {"thermal_limit_emergency", "Emergency thermal limit for "}                      },
      {"emergency",               {"thermal_limit_emergency", "Emergency thermal limit for "}                      },
      {"lcrit",                   {"thermal_limit_critical_low", "Low critical thermal limit for "}                },
      {"critical_low",            {"thermal_limit_critical_low", "Low critical thermal limit for "}                },
      {"low_critical",            {"thermal_limit_critical_low", "Low critical thermal limit for "}                },
      {"max_hyst",                {"thermal_limit_high_hysteresis", "High thermal hysteresis for "}                },
      {"high_hyst",               {"thermal_limit_high_hysteresis", "High thermal hysteresis for "}                },
      {"high_hysteresis",         {"thermal_limit_high_hysteresis", "High thermal hysteresis for "}                },
      {"min_hyst",                {"thermal_limit_low_hysteresis", "Low thermal hysteresis for "}                  },
      {"low_hyst",                {"thermal_limit_low_hysteresis", "Low thermal hysteresis for "}                  },
      {"low_hysteresis",          {"thermal_limit_low_hysteresis", "Low thermal hysteresis for "}                  },
      {"crit_hyst",               {"thermal_limit_critical_hysteresis", "Critical thermal hysteresis for "}        },
      {"critical_hyst",           {"thermal_limit_critical_hysteresis", "Critical thermal hysteresis for "}        },
      {"critical_hysteresis",     {"thermal_limit_critical_hysteresis", "Critical thermal hysteresis for "}        },
      {"lcrit_hyst",              {"thermal_limit_critical_low_hysteresis", "Low critical thermal hysteresis for "}},
      {"critical_low_hyst",       {"thermal_limit_critical_low_hysteresis", "Low critical thermal hysteresis for "}},
      {"critical_low_hysteresis", {"thermal_limit_critical_low_hysteresis", "Low critical thermal hysteresis for "}},
      {"emergency_hyst",          {"thermal_limit_emergency_hysteresis", "Emergency thermal hysteresis for "}      },
      {"emerg_hyst",              {"thermal_limit_emergency_hysteresis", "Emergency thermal hysteresis for "}      },
      {"emergency_hysteresis",    {"thermal_limit_emergency_hysteresis", "Emergency thermal hysteresis for "}      },
      {"average",                 {"average", "Average reading for "}                                              },
      {"average_highest",         {"average_highest", "Highest average reading for "}                              },
      {"average_lowest",          {"average_lowest", "Lowest average reading for "}                                },
      {"input_highest",           {"input_highest", "Highest instantaneous reading for "}                          },
      {"input_lowest",            {"input_lowest", "Lowest instantaneous reading for "}                            },
      {"lowest",                  {"lowest", "Lowest recorded reading for "}                                       },
      {"highest",                 {"highest", "Highest recorded reading for "}                                     },
      {"alarm",                   {"alarm", "Alarm status for "}                                                   },
      {"min_alarm",               {"min_alarm", "Minimum threshold alarm for "}                                    },
      {"low_alarm",               {"min_alarm", "Minimum threshold alarm for "}                                    },
      {"max_alarm",               {"max_alarm", "Maximum threshold alarm for "}                                    },
      {"high_alarm",              {"max_alarm", "Maximum threshold alarm for "}                                    },
      {"crit_alarm",              {"crit_alarm", "Critical threshold alarm for "}                                  },
      {"critical_alarm",          {"crit_alarm", "Critical threshold alarm for "}                                  },
      {"lcrit_alarm",             {"lcrit_alarm", "Low critical threshold alarm for "}                             },
      {"critical_low_alarm",      {"lcrit_alarm", "Low critical threshold alarm for "}                             },
      {"emergency_alarm",         {"emergency_alarm", "Emergency threshold alarm for "}                            },
      {"emerg_alarm",             {"emergency_alarm", "Emergency threshold alarm for "}                            },
      {"fault",                   {"fault", "Fault status for "}                                                   },
      {"beep",                    {"beep", "Beep status for "}                                                     },
      {"beep_enabled",            {"beep", "Beep status for "}                                                     },
      {"sensor_type",             {"sensor_type", "Sensor type for "}                                              },
      {"type",                    {"sensor_type", "Sensor type for "}                                              },
      {"offset",                  {"offset", "Offset for "}                                                        },
      {"div",                     {"div", "Fan divisor for "}                                                      },
      {"divider",                 {"div", "Fan divisor for "}                                                      },
      {"pulses",                  {"pulses", "Fan pulses per revolution for "}                                     },
      {"cap",                     {"cap", "Power cap for "}                                                        },
      {"cap_hyst",                {"cap_hyst", "Power cap hysteresis for "}                                        },
      {"cap_hysteresis",          {"cap_hyst", "Power cap hysteresis for "}                                        },
      {"cap_alarm",               {"cap_alarm", "Power cap alarm status for "}                                     },
      {"average_interval",        {"average_interval", "Power averaging interval for "}                            },
  });

  const auto* const alias_it = std::ranges::find_if(
      k_declared_derived_metric_aliases,
      [normalized_metric_name](const auto& alias) { return alias.alias == normalized_metric_name; });
  if (alias_it == k_declared_derived_metric_aliases.end()) {
    return std::nullopt;
  }
  return alias_it->metric;
}

static auto ParseDeclaredDerivedMetric(std::string_view metric_name)
    -> std::expected<DeclaredDerivedMetric, astl_status_code> {
  const auto normalized_metric_name = astl::ToLowerCopy(std::string{metric_name});
  if (const auto derived_metric = FindDeclaredDerivedMetric(normalized_metric_name); derived_metric.has_value()) {
    return *derived_metric;
  }

  ASTL_LOG_ERROR("Unsupported libsensors derived metric '{}'", metric_name);
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

static auto ResolveDeclaredDerivedMetric(std::string_view                                   configured_metric_name,
                                         const metrics::spec::DerivedMetricJsonDeclaration& derived_metric_declaration)
    -> std::expected<DeclaredDerivedMetric, astl_status_code> {
  if (derived_metric_declaration.register_suffix.has_value()) {
    return DeclaredDerivedMetric{
        .label_suffix       = *derived_metric_declaration.register_suffix,
        .description_prefix = "Derived reading for ",
    };
  }
  return ParseDeclaredDerivedMetric(configured_metric_name);
}

static void ApplyDeclaredDerivedMetricOverrides(
    metrics::spec::MetricJsonDeclaration&              generated_metric_declaration,
    const metrics::spec::DerivedMetricJsonDeclaration& derived_metric_declaration) {
  if (derived_metric_declaration.unit.has_value()) {
    generated_metric_declaration.unit = derived_metric_declaration.unit;
  }
  if (derived_metric_declaration.identifier.has_value()) {
    generated_metric_declaration.identifier = *derived_metric_declaration.identifier;
  }
  if (derived_metric_declaration.metric_type.has_value()) {
    generated_metric_declaration.metric_type = *derived_metric_declaration.metric_type;
  }
  if (derived_metric_declaration.metric_groups.has_value()) {
    generated_metric_declaration.metric_groups = derived_metric_declaration.metric_groups;
  }
}

static auto ExpandDeclaredDerivedMetricsForMetric(metrics::spec::MetricsDeclaration&          metrics_declaration,
                                                  const std::string&                          declared_metric_name,
                                                  const metrics::spec::MetricJsonDeclaration& metric_declaration)
    -> astl_status_code {
  std::unordered_set<std::string> emitted_limit_suffixes;
  const auto base_register_name = std::string{DeclaredRegisterName(declared_metric_name, metric_declaration)};

  for (const auto& [configured_metric_name, derived_metric_declaration] : *metric_declaration.derived_metrics) {
    auto limit_metric_or_error = ResolveDeclaredDerivedMetric(configured_metric_name, derived_metric_declaration);
    if (!limit_metric_or_error.has_value()) {
      return limit_metric_or_error.error();
    }
    const auto& limit_metric = *limit_metric_or_error;
    if (!emitted_limit_suffixes.insert(std::string{limit_metric.label_suffix}).second) {
      continue;
    }

    const auto generated_metric_name = declared_metric_name + "_" + std::string{limit_metric.label_suffix};
    if (metrics_declaration.metrics.contains(generated_metric_name)) {
      continue;
    }

    auto generated_metric_declaration        = metric_declaration;
    generated_metric_declaration.description = derived_metric_declaration.description.value_or(
        std::string{limit_metric.description_prefix} + base_register_name);
    generated_metric_declaration.metric_type = "value";
    generated_metric_declaration.collection.register_name =
        base_register_name + " " + std::string{limit_metric.label_suffix};
    // Avoid unintentionally inheriting the base metric's identifier/units when the
    // derived declaration does not provide explicit overrides for them.
    if (!derived_metric_declaration.identifier.has_value()) {
      generated_metric_declaration.identifier.clear();
    }
    if (!derived_metric_declaration.unit.has_value()) {
      generated_metric_declaration.unit.reset();
    }
    ApplyDeclaredDerivedMetricOverrides(generated_metric_declaration, derived_metric_declaration);
    generated_metric_declaration.derived_metrics   = std::nullopt;
    generated_metric_declaration.inferred_state    = std::nullopt;
    generated_metric_declaration.states            = std::nullopt;
    generated_metric_declaration.finite_set_values = std::nullopt;
    metrics_declaration.metrics.emplace(generated_metric_name, std::move(generated_metric_declaration));
  }

  return ASTL_STATUS_SUCCESS;
}

static auto ExpandDeclaredDerivedMetrics(metrics::spec::MetricsDeclaration& metrics_declaration) -> astl_status_code {
  std::vector<std::pair<std::string, metrics::spec::MetricJsonDeclaration>> original_metrics{
      metrics_declaration.metrics.begin(), metrics_declaration.metrics.end()};

  for (const auto& [declared_metric_name, metric_declaration] : original_metrics) {
    if (!metric_declaration.derived_metrics.has_value()) {
      continue;
    }
    const auto collector_type = metrics::spec::ParseCollectorType(metric_declaration);
    if (!collector_type.has_value() || collector_type.value() != CollectorType::LIBSENSORS) {
      ASTL_LOG_ERROR("derived_metrics is only supported for libsensors metrics, but '{}' uses protocol '{}'",
                     declared_metric_name, metric_declaration.collection.protocol);
      return ASTL_STATUS_BAD_CONFIGURATION;
    }

    const auto expand_status =
        ExpandDeclaredDerivedMetricsForMetric(metrics_declaration, declared_metric_name, metric_declaration);
    if (expand_status != ASTL_STATUS_SUCCESS) {
      return expand_status;
    }
  }

  return ASTL_STATUS_SUCCESS;
}

static auto NormalizeNameComponent(std::string text) -> std::string {
  std::replace_if(
      text.begin(), text.end(), [](char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; },
      '_');
  return text;
}

static auto BuildMetricDescription(const DiscoveredSensorMetric& sensor) -> std::string {
  if (sensor.description.has_value()) {
    return sensor.description.value();
  }
  switch (sensor.identifier) {
    case ASTL_METRIC_IDENTIFIER_TEMPERATURE:
      return "Temperature reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_POWER:
      return "Power reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_FAN_SPEED:
      return "Fan speed reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_VOLTAGE:
      return "Voltage reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_CURRENT:
      return "Current reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_ENERGY:
      return "Energy reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_HUMIDITY:
      return "Humidity reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_THERMAL_LIMIT:
      return "Thermal limit reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_THERMAL_THROTTLE:
      return "Thermal throttle reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_POWER_LIMIT:
      return "Power limit reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_POWER_THROTTLE:
      return "Power throttle reading for " + sensor.label;
    case ASTL_METRIC_IDENTIFIER_STATUS:
      return "Status reading for " + sensor.label;
    default:
      return sensor.label + " reading";
  }
}

static auto BuildDefaultIdentifier(const DiscoveredSensorMetric& sensor) -> astl_metric_identifier_t {
  return sensor.identifier;
}

using OwnedSensorLabel = std::unique_ptr<char, decltype(&std::free)>;

static auto GetOwnedSensorLabel(const sensors_chip_name* chip, const sensors_feature* feature,
                                SensorsApi const* sensors_api) -> OwnedSensorLabel {
  return OwnedSensorLabel{sensors_api->get_label(chip, feature), &std::free};
}

static auto HasUsableInitialValue(const DiscoveredSensorMetric& sensor, SensorsApi const* sensors_api) -> bool {
  double    value  = 0.0;
  const int status = sensors_api->get_value(sensor.chip, sensor.subfeature->number, &value);
  if (status != 0) {
    ASTL_LOG_INFO("Skipping libsensors metric '{}' on chip '{}' because the current value is not available",
                  sensor.label, sensor.chip_name);
    return false;
  }
  if (!std::isfinite(value)) {
    ASTL_LOG_INFO("Skipping libsensors metric '{}' on chip '{}' because the current value is not finite", sensor.label,
                  sensor.chip_name);
    return false;
  }
  return true;
}

static auto FindReadableSubfeature(const sensors_chip_name* chip, const sensors_feature* feature,
                                   sensors_subfeature_type subtype, SensorsApi const* sensors_api)
    -> const sensors_subfeature* {
  const sensors_subfeature* sub = sensors_api->get_subfeature(chip, feature, subtype);
  if (!sub || (sub->flags & SENSORS_MODE_R) == 0) {
    return nullptr;
  }
  return sub;
}

auto GetPrimarySubfeature(const sensors_chip_name* chip, const sensors_feature* feature, SensorsApi const* sensors_api)
    -> std::expected<const sensors_subfeature*, astl_status_code> {
  if (!chip || !feature) {
    ASTL_LOG_ERROR("GetPrimarySubfeature: Invalid chip or feature pointer");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  const auto feature_descriptor = GetFeatureDescriptor(feature->type);
  if (!feature_descriptor.has_value()) {
    ASTL_LOG_WARNING("GetPrimarySubfeature: Unrecognized feature type {}", feature->type);
    return std::unexpected(astl::kInternalNotImplemented);
  }

  const auto primary_subfeatures = std::span<const sensors_subfeature_type>{
      feature_descriptor->primary_subfeatures.data(), feature_descriptor->primary_subfeature_count};
  const sensors_subfeature* readable_subfeature = nullptr;
  const auto has_readable_subfeature = [&chip, &feature, &sensors_api, &readable_subfeature](const auto subtype) {
    readable_subfeature = FindReadableSubfeature(chip, feature, subtype, sensors_api);
    return readable_subfeature != nullptr;
  };
  if (std::ranges::find_if(primary_subfeatures, has_readable_subfeature) != primary_subfeatures.end()) {
    return readable_subfeature;
  }
  auto sensor_label = GetOwnedSensorLabel(chip, feature, sensors_api);
  ASTL_LOG_WARNING("GetPrimarySubfeature: No valid primary subfeature found for {} sensor: {}", feature->name,
                   sensor_label != nullptr ? sensor_label.get() : "<unknown>");
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

/**
 * @brief Build a stable, human-readable label for a discovered sensor.
 */
static auto GetSensorLabel(const sensors_feature* feature, SensorsApi const* sensors_api, const sensors_chip_name* chip)
    -> std::string {
  auto label = GetOwnedSensorLabel(chip, feature, sensors_api);
  if (label != nullptr && *label != '\0') {
    return std::string{label.get()};
  }
  if (feature->name != nullptr && *feature->name != '\0') {
    return feature->name;
  }
  return std::string{"sensor-"} + std::to_string(feature->number);
}

/**
 * @brief Build a unique per-target metric name for a discovered libsensors metric.
 */
static auto BuildConfigMetricName(const DiscoveredSensorMetric&               sensor,
                                  const std::unordered_map<std::string, int>& chip_label_counts) -> std::string {
  const std::string normalized_name = NormalizeNameComponent(sensor.label);
  if (auto chip_label_iter = chip_label_counts.find(sensor.chip_name + " " + sensor.label);
      chip_label_iter != chip_label_counts.end() && chip_label_iter->second == 1) {
    return normalized_name;
  }

  return normalized_name + "_" + std::to_string(sensor.feature->number);
}

static auto FindStableMetricFamilyName(const AstlConfiguration& configuration, std::string_view chip_name)
    -> std::optional<std::string> {
  std::string chip_family{chip_name};
  while (true) {
    const auto separator_position = chip_family.rfind('-');
    if (separator_position == std::string::npos) {
      break;
    }
    chip_family.erase(separator_position);
    const auto metrics_file_path =
        configuration.metrics_dir_path / "libsensors" / ("libsensors_" + chip_family + ".json");
    if (std::filesystem::exists(metrics_file_path)) {
      return chip_family;
    }
  }
  return std::nullopt;
}

static auto BuildMetricNamePrefix(const AstlConfiguration& configuration, const LibsensorsTarget& target,
                                  std::unordered_map<std::string, std::size_t>& instance_counts_by_family)
    -> std::string {
  const auto family_name = FindStableMetricFamilyName(configuration, target.ChipName());
  if (!family_name.has_value()) {
    return target.ChipName();
  }

  const auto instance_index = ++instance_counts_by_family[*family_name];
  return *family_name + "-" + std::to_string(instance_index);
}

static auto BuildFinalMetricName(std::string_view metric_name_prefix, std::string_view config_metric_name)
    -> std::string {
  return NormalizeNameComponent(std::string{metric_name_prefix} + "_" + std::string{config_metric_name});
}

static auto BuildMetricId(const DiscoveredSensorMetric& sensor) -> std::string {
  return std::string{"libsensors::"} + sensor.chip_name +
         "::" + (sensor.feature->name != nullptr ? sensor.feature->name : "unknown") +
         "::" + std::to_string(sensor.subfeature->number);
}

static auto MakeDiscoveredSensorMetric(const sensors_chip_name* chip, const sensors_feature* feature,
                                       const sensors_subfeature* subfeature, std::string chip_name, std::string label,
                                       std::string register_name, astl_units_t units,
                                       astl_metric_identifier_t   identifier,
                                       std::optional<std::string> description = std::nullopt)
    -> DiscoveredSensorMetric {
  return DiscoveredSensorMetric{
      .chip          = chip,
      .feature       = feature,
      .subfeature    = subfeature,
      .chip_name     = std::move(chip_name),
      .label         = std::move(label),
      .register_name = std::move(register_name),
      .units         = units,
      .identifier    = identifier,
      .description   = std::move(description),
  };
}

static auto AppendDiscoveredSensorMetric(std::vector<DiscoveredSensorMetric>& discovered_sensors,
                                         DiscoveredSensorMetric sensor, SensorsApi const* sensors_api) -> void {
  if (HasUsableInitialValue(sensor, sensors_api)) {
    discovered_sensors.push_back(std::move(sensor));
  }
}

static auto AddDerivedSubfeatureMetrics(std::vector<DiscoveredSensorMetric>& discovered_sensors,
                                        const sensors_chip_name* chip, const sensors_feature* feature,
                                        const sensors_subfeature* primary_subfeature, const std::string& chip_name,
                                        const std::string& label, SensorsApi const* sensors_api,
                                        std::unordered_set<std::string>& observed_register_names) -> void {
  const auto feature_descriptor = GetFeatureDescriptor(feature->type);
  if (!feature_descriptor.has_value()) {
    return;
  }

  for (const auto& derived_metric : feature_descriptor->derived_subfeatures) {
    if (primary_subfeature != nullptr && primary_subfeature->type == derived_metric.subtype) {
      continue;
    }
    const sensors_subfeature* subfeature = FindReadableSubfeature(chip, feature, derived_metric.subtype, sensors_api);
    if (!subfeature) {
      continue;
    }
    const std::string label_register_name = label + " " + derived_metric.register_suffix;
    const std::string feature_name        = feature->name != nullptr ? feature->name : "";
    const std::string feature_register_name =
        feature_name.empty() ? std::string{} : feature_name + " " + derived_metric.register_suffix;
    RecordObservedSensorNames(observed_register_names, label_register_name, feature_register_name);
    const auto units = ResolveUnitsFromPolicy(feature_descriptor->units, derived_metric.units_policy);
    const auto identifier =
        ResolveIdentifierFromPolicy(feature_descriptor->identifier, derived_metric.identifier_policy);
    AppendDiscoveredSensorMetric(
        discovered_sensors,
        MakeDiscoveredSensorMetric(chip, feature, subfeature, chip_name, label_register_name,
                                   feature_register_name.empty() ? label_register_name : feature_register_name, units,
                                   identifier, std::string{derived_metric.description_prefix} + label),
        sensors_api);
  }
}

static auto BuildDeclarationLookupNames(const LibsensorsTarget& target) -> std::vector<std::pair<std::string, bool>> {
  std::vector<std::pair<std::string, bool>> lookup_names;
  const auto                                exact_lookup_name = std::string{"libsensors_"} + target.ChipName();
  lookup_names.emplace_back(exact_lookup_name, true);

  std::unordered_set<std::string> seen_names{exact_lookup_name};
  std::string                     chip_family = target.ChipName();
  while (true) {
    const auto separator_position = chip_family.rfind('-');
    if (separator_position == std::string::npos) {
      break;
    }
    chip_family.erase(separator_position);
    const auto candidate_name = std::string{"libsensors_"} + chip_family;
    if (!seen_names.contains(candidate_name)) {
      lookup_names.emplace_back(candidate_name, false);
      seen_names.insert(candidate_name);
    }
  }

  return lookup_names;
}

static auto LoadMetricDeclarationsForTarget(const AstlConfiguration& configuration, const LibsensorsTarget& target)
    -> std::expected<LoadedMetricDeclarations, astl_status_code> {
  const auto lookup_names = BuildDeclarationLookupNames(target);
  for (const auto& [lookup_name, is_exact_match] : lookup_names) {
    const auto metrics_file_path = configuration.metrics_dir_path / "libsensors" / (lookup_name + ".json");
    if (!std::filesystem::exists(metrics_file_path)) {
      continue;
    }

    std::unordered_set<std::string> active_paths;
    auto merged_json_or_error = LoadMergedMetricsDeclarationJson(metrics_file_path, active_paths);
    if (!merged_json_or_error.has_value()) {
      return std::unexpected(merged_json_or_error.error());
    }

    metrics::spec::MetricsDeclaration declarations;
    try {
      declarations = merged_json_or_error->get<metrics::spec::MetricsDeclaration>();
    } catch (std::exception const& e) {
      ASTL_LOG_ERROR("Unable to parse merged libsensors metrics declaration file {}: {}", metrics_file_path.string(),
                     e.what());
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    const auto expand_status = ExpandDeclaredDerivedMetrics(declarations);
    if (expand_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(expand_status);
    }

    if (is_exact_match) {
      ASTL_LOG_INFO("Loaded exact libsensors metrics declaration file for target '{}' from {}", target.Name(),
                    metrics_file_path.string());
    } else {
      ASTL_LOG_INFO("Loaded fallback libsensors metrics declaration file for target '{}' from {}", target.Name(),
                    metrics_file_path.string());
    }

    return LoadedMetricDeclarations{
        .declarations = std::move(declarations),
        .match_kind   = is_exact_match ? LibsensorsTargetContext::DeclarationMatchKind::EXACT
                                       : LibsensorsTargetContext::DeclarationMatchKind::FALLBACK,
    };
  }

  ASTL_LOG_INFO("No libsensors metrics declaration file found for target '{}'", target.Name());
  return LoadedMetricDeclarations{};
}

static auto MakeDefaultRegistrationDetails(std::string_view metric_name_prefix, const DiscoveredSensorMetric& sensor,
                                           std::string_view config_metric_name)
    -> std::optional<LibsensorsMetricRegistrationDetails> {
  return std::optional<LibsensorsMetricRegistrationDetails>{
      LibsensorsMetricRegistrationDetails{
                                          .name          = BuildFinalMetricName(metric_name_prefix, config_metric_name),
                                          .description   = BuildMetricDescription(sensor),
                                          .units         = sensor.units,
                                          .identifier    = BuildDefaultIdentifier(sensor),
                                          .metric_type   = ASTL_METRIC_VALUE,
                                          .metric_groups = {},
                                          .formula       = AnyFormula{IdentityFormula{}},
                                          }
  };
}

using MetricsDeclarationMap = decltype(std::declval<metrics::spec::MetricsDeclaration>().metrics);

static auto FindMatchingMetricDeclaration(const metrics::spec::MetricsDeclaration& declarations,
                                          std::string_view config_metric_name, const DiscoveredSensorMetric& sensor)
    -> MetricsDeclarationMap::const_iterator {
  auto declaration_iter = declarations.metrics.find(std::string{config_metric_name});
  if (declaration_iter != declarations.metrics.end()) {
    return declaration_iter;
  }
  return std::ranges::find_if(declarations.metrics, [&sensor](const auto& declaration_entry) {
    const auto& [declared_metric_name, metric_declaration] = declaration_entry;
    return SensorMatchesDeclaredRegister(sensor, DeclaredRegisterName(declared_metric_name, metric_declaration));
  });
}

static auto ResolveUndeclaredMetricRegistration(std::string_view config_metric_name, std::string_view final_metric_name,
                                                const LibsensorsTargetContext& target_context,
                                                const DiscoveredSensorMetric&  sensor)
    -> std::optional<LibsensorsMetricRegistrationDetails> {
  if (target_context.declaration_match_kind == LibsensorsTargetContext::DeclarationMatchKind::FALLBACK) {
    ASTL_LOG_INFO(
        "Registering discovered libsensors metric '{}' on chip '{}' using default metadata because it was not "
        "declared in the fallback family config",
        final_metric_name, sensor.chip_name);
    return MakeDefaultRegistrationDetails(target_context.metric_name_prefix, sensor, config_metric_name);
  }
  ASTL_LOG_WARNING(
      "Skipping discovered libsensors metric '{}' on chip '{}' because it is not declared in the exact "
      "target config allowlist",
      final_metric_name, sensor.chip_name);
  return std::nullopt;
}

static auto ValidateDeclaredMetric(const DiscoveredSensorMetric& sensor, std::string_view final_metric_name,
                                   std::string_view                            declared_metric_name,
                                   const metrics::spec::MetricJsonDeclaration& metric_declaration) -> astl_status_code {
  auto collector_type = metrics::spec::ParseCollectorType(metric_declaration);
  if (!collector_type.has_value() || collector_type.value() != CollectorType::LIBSENSORS) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' has invalid collector protocol '{}'", final_metric_name,
                   sensor.chip_name, metric_declaration.collection.protocol);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  const auto declared_register_name = std::string{DeclaredRegisterName(declared_metric_name, metric_declaration)};
  if (!declared_register_name.empty() && !SensorMatchesDeclaredRegister(sensor, declared_register_name)) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' expects register '{}' but discovered '{}'", final_metric_name,
                   sensor.chip_name, declared_register_name, sensor.register_name);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return ASTL_STATUS_SUCCESS;
}

static auto ResolveDeclaredUnits(const DiscoveredSensorMetric& sensor, std::string_view final_metric_name,
                                 const metrics::spec::MetricJsonDeclaration& metric_declaration)
    -> std::expected<astl_units_t, astl_status_code> {
  if (!metric_declaration.unit.has_value()) {
    return sensor.units;
  }

  const auto units = ParseUnits(metric_declaration.unit.value());
  if (units == ASTL_UNITS_UNKNOWN) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' uses unsupported unit '{}'", final_metric_name,
                   sensor.chip_name, metric_declaration.unit.value());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (units != sensor.units) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' overrides units from '{}' to '{}', which is not supported",
                   final_metric_name, sensor.chip_name, std::to_string(sensor.units), std::to_string(units));
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return units;
}

static auto ResolveDeclaredMetricType(std::string_view final_metric_name, const DiscoveredSensorMetric& sensor,
                                      const metrics::spec::MetricJsonDeclaration& metric_declaration)
    -> std::expected<astl_metric_type_t, astl_status_code> {
  const auto metric_type =
      metric_declaration.metric_type.empty() ? ASTL_METRIC_VALUE : ParseMetricType(metric_declaration.metric_type);
  if (metric_type == ASTL_METRIC_UNKNOWN) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' has unsupported metric type '{}'", final_metric_name,
                   sensor.chip_name, metric_declaration.metric_type);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return metric_type;
}

static auto BuildDeclaredRegistrationDetails(const DiscoveredSensorMetric& sensor, std::string_view final_metric_name,
                                             std::string_view                            declared_metric_name,
                                             const metrics::spec::MetricJsonDeclaration& metric_declaration)
    -> std::expected<LibsensorsMetricRegistrationDetails, astl_status_code> {
  const auto validation_status =
      ValidateDeclaredMetric(sensor, final_metric_name, declared_metric_name, metric_declaration);
  if (validation_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(validation_status);
  }

  auto units_or_error = ResolveDeclaredUnits(sensor, final_metric_name, metric_declaration);
  if (!units_or_error.has_value()) {
    return std::unexpected(units_or_error.error());
  }

  auto formula_result = BuildFormula(metric_declaration.formula);
  if (!formula_result.has_value()) {
    ASTL_LOG_ERROR("Failed to build formula for libsensors metric '{}' on chip '{}': {}", final_metric_name,
                   sensor.chip_name, astlStatusString(formula_result.error()));
    return std::unexpected(formula_result.error());
  }

  auto metric_type_or_error = ResolveDeclaredMetricType(final_metric_name, sensor, metric_declaration);
  if (!metric_type_or_error.has_value()) {
    return std::unexpected(metric_type_or_error.error());
  }

  return LibsensorsMetricRegistrationDetails{
      .name = std::string{final_metric_name},
      .description =
          metric_declaration.description.empty() ? BuildMetricDescription(sensor) : metric_declaration.description,
      .units         = *units_or_error,
      .identifier    = metric_declaration.identifier.empty() ? BuildDefaultIdentifier(sensor)
                                                             : ParseMetricIdentifier(metric_declaration.identifier),
      .metric_type   = *metric_type_or_error,
      .metric_groups = metric_declaration.metric_groups.value_or(std::vector<std::string>{}),
      .formula       = std::move(formula_result.value()),
  };
}

static auto ResolveMetricRegistrationDetails(const DiscoveredSensorMetric&  sensor,
                                             const std::string&             config_metric_name,
                                             const LibsensorsTargetContext& target_context)
    -> std::expected<std::optional<LibsensorsMetricRegistrationDetails>, astl_status_code> {
  const auto& final_metric_name_prefix = target_context.metric_name_prefix;
  const auto  final_metric_name        = BuildFinalMetricName(final_metric_name_prefix, config_metric_name);
  if (!target_context.declarations.has_value()) {
    return MakeDefaultRegistrationDetails(final_metric_name_prefix, sensor, config_metric_name);
  }

  auto declaration_iter = FindMatchingMetricDeclaration(*target_context.declarations, config_metric_name, sensor);
  if (declaration_iter == target_context.declarations->metrics.end()) {
    return ResolveUndeclaredMetricRegistration(config_metric_name, final_metric_name, target_context, sensor);
  }

  auto registration_details_or_error =
      BuildDeclaredRegistrationDetails(sensor, final_metric_name, declaration_iter->first, declaration_iter->second);
  if (!registration_details_or_error.has_value()) {
    return std::unexpected(registration_details_or_error.error());
  }
  return std::optional<LibsensorsMetricRegistrationDetails>{std::move(registration_details_or_error.value())};
}

static auto WarnAboutUndiscoveredDeclaredMetrics(const LibsensorsTargetContext&         target_context,
                                                 const std::unordered_set<std::string>& observed_register_names)
    -> void {
  if (!target_context.declarations.has_value() ||
      target_context.declaration_match_kind != LibsensorsTargetContext::DeclarationMatchKind::EXACT) {
    return;
  }

  for (const auto& [declared_metric_name, metric_declaration] : target_context.declarations->metrics) {
    const auto declared_register_name = DeclaredRegisterName(declared_metric_name, metric_declaration);
    if (!observed_register_names.contains(std::string{declared_register_name})) {
      ASTL_LOG_WARNING(
          "Libsensors metric '{}' (register '{}') is declared for target '{}' but was not observed on the "
          "current system",
          declared_metric_name, declared_register_name, target_context.target->Name());
    }
  }
}

/**
 * @brief Discover supported sensors from a detected chip.
 */
static auto DiscoverSensorsFromChip(const astl::AstlConfiguration& configuration, const sensors_chip_name* chip,
                                    SensorsApi const* sensors_api, std::string& chip_name_out,
                                    std::unordered_set<std::string>& observed_register_names)
    -> std::expected<std::vector<DiscoveredSensorMetric>, astl_status_code> {
  (void)configuration;
  const sensors_feature*              feature              = nullptr;
  int                                 sensor_feature_count = 0;
  constexpr size_t                    max_name_length      = 200;
  std::array<char, max_name_length>   chip_name{'\0'};
  std::vector<DiscoveredSensorMetric> discovered_sensors;
  sensors_api->snprintf_chip_name(chip_name.data(), max_name_length, chip);
  chip_name_out = chip_name.data();
  ASTL_LOG_INFO("Scanning {} for features", chip_name.data());
  while ((feature = sensors_api->get_features(chip, &sensor_feature_count))) {
    const std::string label         = GetSensorLabel(feature, sensors_api, chip);
    const std::string register_name = feature->name != nullptr ? feature->name : "";
    RecordObservedSensorNames(observed_register_names, label, register_name);
    ASTL_LOG_DEBUG("  Found sensor: `{}` type {} with name {}", label, feature->type,
                   feature->name != nullptr ? feature->name : "<null>");
    const auto sub = GetPrimarySubfeature(chip, feature, sensors_api);
    if (!sub) {
      if (sub.error() == astl::kInternalNotImplemented) {
        continue;
      }
      return std::unexpected(sub.error());
    }

    const auto feature_descriptor = GetFeatureDescriptor(feature->type);
    if (!feature_descriptor.has_value()) {
      ASTL_LOG_WARNING("Unrecognized feature type {}, skipping this feature.", feature->type);
      continue;
    }
    discovered_sensors.push_back(DiscoveredSensorMetric{
        .chip          = chip,
        .feature       = feature,
        .subfeature    = *sub,
        .chip_name     = chip_name.data(),
        .label         = label,
        .register_name = register_name,
        .units         = feature_descriptor->units,
        .identifier    = feature_descriptor->identifier,
        .description   = std::nullopt,
    });
    if (!HasUsableInitialValue(discovered_sensors.back(), sensors_api)) {
      discovered_sensors.pop_back();
      continue;
    }
    AddDerivedSubfeatureMetrics(discovered_sensors, chip, feature, *sub, chip_name.data(), label, sensors_api,
                                observed_register_names);
  }
  return discovered_sensors;
}

/**
 * @brief Register a discovered sensor using a conflict-free metric name.
 */
static auto RegisterSensorMetric(const DiscoveredSensorMetric&                                   sensor,
                                 const std::unordered_map<std::string, int>&                     chip_label_counts,
                                 IMetricManager*                                                 metric_manager,
                                 const std::unordered_map<std::string, LibsensorsTargetContext>& target_contexts)
    -> astl_status_code {
  const auto target_context_iter = target_contexts.find(sensor.chip_name);
  if (target_context_iter == target_contexts.end()) {
    ASTL_LOG_ERROR("No Libsensors target found for chip '{}'", sensor.chip_name);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  const std::string config_metric_name = BuildConfigMetricName(sensor, chip_label_counts);
  auto details_or_error = ResolveMetricRegistrationDetails(sensor, config_metric_name, target_context_iter->second);
  if (!details_or_error.has_value()) {
    return details_or_error.error();
  }
  if (!details_or_error->has_value()) {
    return ASTL_STATUS_SUCCESS;
  }
  auto& details = details_or_error->value();

  auto metric_config = std::make_unique<MetricConfig>(
      details.name, details.description, details.units, ASTL_VALUE_FLOAT64, details.identifier, details.metric_type,
      CollectorType::LIBSENSORS, LibsensorsOperationBuilder{sensor.chip, sensor.subfeature->number},
      std::move(details.formula), ASTL_VALUE_UNKNOWN, std::move(details.metric_groups), BuildMetricId(sensor));

  return metric_manager->RegisterMetric(std::move(metric_config), {target_context_iter->second.target});
}
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)

/**
 * @brief Scan the collector_type_to_targets_map for Libsensors targets.
 *        Use the given configuration to create metrics and register them in the metric_manager.
 */
auto RegisterLibsensorsMetrics(
    const AstlConfiguration&                                              configuration,
    const std::unordered_map<CollectorType, std::vector<const ITarget*>>& collector_type_to_targets_map,
    // cppcheck-suppress constParameterReference // metric_manager is modified if libsensors available
    IMetricManager* metric_manager) -> astl_status_code {
  if (!metric_manager) {
    ASTL_LOG_ERROR("metric_manager is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
#if !defined(ASTL_INCLUDE_LIBSENSORS)
  (void)configuration;
#endif
#if defined(ASTL_INCLUDE_LIBSENSORS)
  auto libsensors_targets_iter = collector_type_to_targets_map.find(CollectorType::LIBSENSORS);
  if (libsensors_targets_iter == collector_type_to_targets_map.end()) {
    ASTL_LOG_INFO("No targets with LIBSENSORS collector type found, skipping LIBSENSORS metric registration");
    return ASTL_STATUS_SUCCESS;
  }
  const auto& libsensors_targets = libsensors_targets_iter->second;
  if (libsensors_targets.empty()) {
    ASTL_LOG_INFO("LIBSENSORS collector type found, but target list is empty; skipping LIBSENSORS metric registration");
    return ASTL_STATUS_SUCCESS;
  }
  // get the api wrapper for dynamically linked libsensors library.
  // should have been loaded and put in Targets by the topology plugin.
  const auto* first_libsensors_target = dynamic_cast<const astl::LibsensorsTarget*>(libsensors_targets.front());
  if (!first_libsensors_target) {
    ASTL_LOG_ERROR("First Libsensors target cannot be cast to LibsensorsTarget type");
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  auto sensors_api = first_libsensors_target->ShareApi();
  if (!sensors_api) {
    ASTL_LOG_ERROR("No valid Libsensors API found in targets, cannot register libsensors metrics.");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  std::unordered_map<std::string, LibsensorsTargetContext> target_contexts;
  target_contexts.reserve(libsensors_targets.size());
  std::unordered_map<std::string, std::size_t> instance_counts_by_family;
  for (const auto* target : libsensors_targets) {
    const auto* libsensors_target = dynamic_cast<const astl::LibsensorsTarget*>(target);
    if (!libsensors_target) {
      ASTL_LOG_ERROR("Libsensors target cannot be cast to LibsensorsTarget type");
      return ASTL_STATUS_BAD_CONFIGURATION;
    }
    auto declarations_or_error = LoadMetricDeclarationsForTarget(configuration, *libsensors_target);
    if (!declarations_or_error.has_value()) {
      return declarations_or_error.error();
    }
    target_contexts.emplace(
        libsensors_target->ChipName(),
        LibsensorsTargetContext{
            .target             = libsensors_target,
            .declarations       = std::move(declarations_or_error->declarations),
            .metric_name_prefix = BuildMetricNamePrefix(configuration, *libsensors_target, instance_counts_by_family),
            .declaration_match_kind = declarations_or_error->match_kind,
        });
  }

  const sensors_chip_name*                                         chip       = nullptr;
  int                                                              chip_index = 0;
  std::vector<DiscoveredSensorMetric>                              discovered_sensors;
  std::unordered_map<std::string, std::unordered_set<std::string>> observed_register_names_by_chip;
  // Chip names must be globally unique because they seed the libsensors metric ids. The live
  // libsensors library can expose several physical chips that share a name (e.g. two
  // "power_meter-acpi-0" instances). Topology discovery already discards the duplicate target, so
  // walk only the first chip of each name here to stay consistent and avoid duplicate metric ids.
  std::unordered_set<std::string> processed_chip_names;
  while ((chip = sensors_api->get_detected_chips(nullptr, &chip_index))) {
    std::string                     chip_name;
    std::unordered_set<std::string> observed_register_names;
    auto                            discovered_or_error =
        DiscoverSensorsFromChip(configuration, chip, sensors_api.get(), chip_name, observed_register_names);
    if (!discovered_or_error.has_value()) {
      return discovered_or_error.error();
    }
    if (!processed_chip_names.insert(chip_name).second) {
      ASTL_LOG_WARNING("Duplicate libsensors chip name '{}'; discarding the duplicate chip's metrics and continuing",
                       chip_name);
      continue;
    }
    observed_register_names_by_chip[chip_name].insert(observed_register_names.begin(), observed_register_names.end());
    auto& chip_sensors = discovered_or_error.value();
    discovered_sensors.insert(discovered_sensors.end(), std::make_move_iterator(chip_sensors.begin()),
                              std::make_move_iterator(chip_sensors.end()));
  }

  std::unordered_map<std::string, int> chip_label_counts;
  for (const auto& sensor : discovered_sensors) {
    ++chip_label_counts[sensor.chip_name + " " + sensor.label];
  }

  std::unordered_map<std::string, std::unordered_set<std::string>> discovered_config_metric_names_by_chip;
  for (const auto& sensor : discovered_sensors) {
    discovered_config_metric_names_by_chip[sensor.chip_name].insert(BuildConfigMetricName(sensor, chip_label_counts));
    auto status = RegisterSensorMetric(sensor, chip_label_counts, metric_manager, target_contexts);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }

  for (const auto& [chip_name, target_context] : target_contexts) {
    if (const auto observed_iter = observed_register_names_by_chip.find(chip_name);
        observed_iter != observed_register_names_by_chip.end()) {
      WarnAboutUndiscoveredDeclaredMetrics(target_context, observed_iter->second);
    } else {
      WarnAboutUndiscoveredDeclaredMetrics(target_context, {});
    }
  }
#else
  (void)collector_type_to_targets_map;
  (void)metric_manager;
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
