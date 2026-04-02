// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "libsensors/libsensors_metric_builder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl_utils.hpp"
#include "config/astl_configuration.hpp"
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
  astl_units_t               units;
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
};

template <typename SpecType>
static auto TryParseJson(std::filesystem::path const& json_file_path) -> std::expected<SpecType, astl_status_code> {
  try {
    std::ifstream json_file{json_file_path};
    json_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    auto json_data   = nlohmann::json::parse(json_file);
    auto parsed_data = json_data.get<SpecType>();
    return parsed_data;
  } catch (std::ifstream::failure const& e) {
    ASTL_LOG_ERROR("Unable to open json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  } catch (nlohmann::json::exception const& e) {
    ASTL_LOG_ERROR("Unable to parse json file {}: {}", json_file_path.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
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
  switch (sensor.feature->type) {
    case SENSORS_FEATURE_TEMP:
      return "Temperature reading for " + sensor.label;
    case SENSORS_FEATURE_POWER:
      return "Power reading for " + sensor.label;
    case SENSORS_FEATURE_FAN:
      return "Fan speed reading for " + sensor.label;
    default:
      return sensor.label + " reading";
  }
}

using OwnedSensorLabel = std::unique_ptr<char, decltype(&std::free)>;

static auto GetOwnedSensorLabel(const sensors_chip_name* chip, const sensors_feature* feature,
                                SensorsApi const* sensors_api) -> OwnedSensorLabel {
  return OwnedSensorLabel{sensors_api->get_label(chip, feature), &std::free};
}

static auto BuildDefaultIdentifier(const DiscoveredSensorMetric& sensor) -> astl_metric_identifier_t {
  switch (sensor.feature->type) {
    case SENSORS_FEATURE_TEMP:
      return ASTL_METRIC_IDENTIFIER_TEMPERATURE;
    case SENSORS_FEATURE_POWER:
      return ASTL_METRIC_IDENTIFIER_POWER;
    case SENSORS_FEATURE_FAN:
      return ASTL_METRIC_IDENTIFIER_FAN_SPEED;
    default:
      return ASTL_METRIC_IDENTIFIER_UNKNOWN;
  }
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

auto GetSubfeature(const sensors_chip_name* chip, const sensors_feature* feature, SensorsApi const* sensors_api)
    -> std::expected<const sensors_subfeature*, astl_status_code> {
  if (!chip || !feature) {
    ASTL_LOG_ERROR("GetSubfeature: Invalid chip or feature pointer");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  // @todo(ASTL-187) Extend libsensors support with additional features/subfeatures
  sensors_subfeature_type subtype = SENSORS_SUBFEATURE_UNKNOWN;
  switch (feature->type) {
    case SENSORS_FEATURE_TEMP:
      subtype = SENSORS_SUBFEATURE_TEMP_INPUT;
      break;
    case SENSORS_FEATURE_POWER:
      subtype = SENSORS_SUBFEATURE_POWER_INPUT;
      break;
    case SENSORS_FEATURE_FAN:
      subtype = SENSORS_SUBFEATURE_FAN_INPUT;
      break;
    default:
      ASTL_LOG_WARNING("GetSubfeature: Unrecognized feature type {}", feature->type);
      return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  const sensors_subfeature* sub = FindReadableSubfeature(chip, feature, subtype, sensors_api);
  if (!sub) {
    const auto label = GetOwnedSensorLabel(chip, feature, sensors_api);
    ASTL_LOG_WARNING("GetSubfeature: No valid input subfeature found for {} sensor: {}",
                     feature->name != nullptr ? feature->name : "<unnamed>",
                     label != nullptr ? label.get() : "<unknown>");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return sub;
}

/**
 * @brief Parse units from sensor feature type
 * libsensors provides temperature in degrees Celsius, power in Watts and fan speed in RPM
 */
static auto ParseUnitsFromFeatureType(sensors_feature_type feature_type) -> astl_units_t {
  switch (feature_type) {
    case SENSORS_FEATURE_TEMP:
      return ASTL_UNITS_CELSIUS;
    case SENSORS_FEATURE_POWER:
      return ASTL_UNITS_WATTS;
    case SENSORS_FEATURE_FAN:
      return ASTL_UNITS_RPM;
    default:
      return ASTL_UNITS_UNKNOWN;
  }
}

/**
 * @brief Build a stable, human-readable label for a discovered sensor.
 */
static auto GetSensorLabel(const sensors_feature* feature, SensorsApi const* sensors_api, const sensors_chip_name* chip)
    -> std::string {
  if (auto label = GetOwnedSensorLabel(chip, feature, sensors_api); label != nullptr && *label != '\0') {
    return NormalizeNameComponent(label.get());
  }
  if (feature->name != nullptr && *feature->name != '\0') {
    return NormalizeNameComponent(feature->name);
  }
  return NormalizeNameComponent(std::string{"sensor-"} + std::to_string(feature->number));
}

/**
 * @brief Build a unique per-target metric name for a discovered libsensors metric.
 */
static auto BuildConfigMetricName(const DiscoveredSensorMetric&               sensor,
                                  const std::unordered_map<std::string, int>& chip_label_counts) -> std::string {
  const std::string& normalized_name = sensor.label;
  if (auto chip_label_iter = chip_label_counts.find(sensor.chip_name + " " + sensor.label);
      chip_label_iter != chip_label_counts.end() && chip_label_iter->second == 1) {
    return normalized_name;
  }

  return normalized_name + "_" + std::to_string(sensor.feature->number);
}

static auto BuildFinalMetricName(const DiscoveredSensorMetric& sensor, std::string_view config_metric_name)
    -> std::string {
  return NormalizeNameComponent(sensor.chip_name + "_" + std::string{config_metric_name});
}

static auto MakeDiscoveredSensorMetric(const sensors_chip_name* chip, const sensors_feature* feature,
                                       const sensors_subfeature* subfeature, std::string chip_name, std::string label,
                                       astl_units_t units, std::optional<std::string> description = std::nullopt)
    -> DiscoveredSensorMetric {
  return DiscoveredSensorMetric{
      .chip        = chip,
      .feature     = feature,
      .subfeature  = subfeature,
      .chip_name   = std::move(chip_name),
      .label       = std::move(label),
      .units       = units,
      .description = std::move(description),
  };
}

static auto AppendDiscoveredSensorMetric(std::vector<DiscoveredSensorMetric>& discovered_sensors,
                                         DiscoveredSensorMetric sensor, SensorsApi const* sensors_api) -> void {
  if (HasUsableInitialValue(sensor, sensors_api)) {
    discovered_sensors.push_back(std::move(sensor));
  }
}

static auto AddOptionalTemperatureLimitMetrics(std::vector<DiscoveredSensorMetric>& discovered_sensors,
                                               const sensors_chip_name* chip, const sensors_feature* feature,
                                               const std::string& chip_name, const std::string& label,
                                               SensorsApi const* sensors_api) -> void {
  struct OptionalLimitMetric {
    sensors_subfeature_type subtype;
    const char*             label_suffix;
    const char*             description_prefix;
  };

  constexpr std::array<OptionalLimitMetric, 3> limit_metrics{
      {
       {SENSORS_SUBFEATURE_TEMP_MIN, "thermal_limit_low", "Low thermal limit for "},
       {SENSORS_SUBFEATURE_TEMP_MAX, "thermal_limit_high", "High thermal limit for "},
       {SENSORS_SUBFEATURE_TEMP_CRIT, "thermal_limit_critical", "Critical thermal limit for "},
       }
  };

  for (const auto& limit_metric : limit_metrics) {
    const sensors_subfeature* subfeature = FindReadableSubfeature(chip, feature, limit_metric.subtype, sensors_api);
    if (!subfeature) {
      continue;
    }
    AppendDiscoveredSensorMetric(
        discovered_sensors,
        MakeDiscoveredSensorMetric(chip, feature, subfeature, chip_name, label + "_" + limit_metric.label_suffix,
                                   ASTL_UNITS_CELSIUS, std::string{limit_metric.description_prefix} + label),
        sensors_api);
  }
}

static auto LoadMetricDeclarationsForTarget(const AstlConfiguration& configuration, const LibsensorsTarget& target)
    -> std::expected<std::optional<metrics::spec::MetricsDeclaration>, astl_status_code> {
  const auto metrics_file_path = configuration.metrics_dir_path / "libsensors" / (target.Name() + ".json");
  if (!std::filesystem::exists(metrics_file_path)) {
    ASTL_LOG_INFO("No libsensors metrics declaration file found for target '{}' at {}", target.Name(),
                  metrics_file_path.string());
    return std::optional<metrics::spec::MetricsDeclaration>{std::nullopt};
  }
  auto declarations_or_error = TryParseJson<metrics::spec::MetricsDeclaration>(metrics_file_path);
  if (!declarations_or_error.has_value()) {
    return std::unexpected(declarations_or_error.error());
  }
  ASTL_LOG_INFO("Loaded libsensors metrics declaration file for target '{}' from {}", target.Name(),
                metrics_file_path.string());
  return std::optional<metrics::spec::MetricsDeclaration>{std::move(declarations_or_error.value())};
}

static auto ResolveMetricRegistrationDetails(
    const DiscoveredSensorMetric& sensor, std::string config_metric_name,
    const std::optional<metrics::spec::MetricsDeclaration>& metric_declarations)
    -> std::expected<std::optional<LibsensorsMetricRegistrationDetails>, astl_status_code> {
  const auto final_metric_name = BuildFinalMetricName(sensor, config_metric_name);
  if (!metric_declarations.has_value()) {
    return std::optional<LibsensorsMetricRegistrationDetails>{
        LibsensorsMetricRegistrationDetails{
                                            .name          = final_metric_name,
                                            .description   = BuildMetricDescription(sensor),
                                            .units         = sensor.units,
                                            .identifier    = BuildDefaultIdentifier(sensor),
                                            .metric_type   = ASTL_METRIC_VALUE,
                                            .metric_groups = {},
                                            .formula       = AnyFormula{IdentityFormula{}},
                                            }
    };
  }

  const auto declaration_iter = metric_declarations->metrics.find(config_metric_name);
  if (declaration_iter == metric_declarations->metrics.end()) {
    ASTL_LOG_INFO("Skipping libsensors metric '{}' on chip '{}' because it is not declared in the target config",
                  final_metric_name, sensor.chip_name);
    return std::optional<LibsensorsMetricRegistrationDetails>{std::nullopt};
  }

  const auto& metric_declaration = declaration_iter->second;
  auto        collector_type     = metrics::spec::ParseCollectorType(metric_declaration);
  if (!collector_type.has_value() || collector_type.value() != CollectorType::LIBSENSORS) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' has invalid collector protocol '{}'", final_metric_name,
                   sensor.chip_name, metric_declaration.collection.protocol);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (!metric_declaration.collection.register_name.empty() &&
      NormalizeNameComponent(metric_declaration.collection.register_name) != sensor.label) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' expects register '{}' but discovered '{}'", final_metric_name,
                   sensor.chip_name, metric_declaration.collection.register_name, sensor.label);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  astl_units_t units = sensor.units;
  if (metric_declaration.unit.has_value()) {
    units = ParseUnits(metric_declaration.unit.value());
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
  }

  auto formula_result = BuildFormula(metric_declaration.formula);
  if (!formula_result.has_value()) {
    ASTL_LOG_ERROR("Failed to build formula for libsensors metric '{}' on chip '{}': {}", final_metric_name,
                   sensor.chip_name, astlStatusString(formula_result.error()));
    return std::unexpected(formula_result.error());
  }
  auto metric_type =
      metric_declaration.metric_type.empty() ? ASTL_METRIC_VALUE : ParseMetricType(metric_declaration.metric_type);
  if (metric_type == ASTL_METRIC_UNKNOWN) {
    ASTL_LOG_ERROR("Libsensors metric '{}' on chip '{}' has unsupported metric type '{}'", final_metric_name,
                   sensor.chip_name, metric_declaration.metric_type);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  return std::optional<LibsensorsMetricRegistrationDetails>{
      LibsensorsMetricRegistrationDetails{
                                          .name = final_metric_name,
                                          .description =
              metric_declaration.description.empty() ? BuildMetricDescription(sensor) : metric_declaration.description,
                                          .units         = units,
                                          .identifier    = metric_declaration.identifier.empty() ? BuildDefaultIdentifier(sensor)
                                                                 : ParseMetricIdentifier(metric_declaration.identifier),
                                          .metric_type   = metric_type,
                                          .metric_groups = metric_declaration.metric_groups.value_or(std::vector<std::string>{}),
                                          .formula       = std::move(formula_result.value()),
                                          }
  };
}

/**
 * @brief Discover supported sensors from a detected chip.
 */
static auto DiscoverSensorsFromChip(const astl::AstlConfiguration& configuration, const sensors_chip_name* chip,
                                    SensorsApi const* sensors_api)
    -> std::expected<std::vector<DiscoveredSensorMetric>, astl_status_code> {
  (void)configuration;
  const sensors_feature*              feature              = nullptr;
  int                                 sensor_feature_count = 0;
  constexpr size_t                    max_name_length      = 200;
  std::array<char, max_name_length>   chip_name{'\0'};
  std::vector<DiscoveredSensorMetric> discovered_sensors;
  sensors_api->snprintf_chip_name(chip_name.data(), max_name_length, chip);
  ASTL_LOG_INFO("Scanning {} for features", chip_name.data());
  while ((feature = sensors_api->get_features(chip, &sensor_feature_count))) {
    const std::string label = GetSensorLabel(feature, sensors_api, chip);
    ASTL_LOG_DEBUG("  Found sensor: `{}` type {} with name {}", label, feature->type,
                   feature->name != nullptr ? feature->name : "<null>");
    const auto sub = GetSubfeature(chip, feature, sensors_api);
    if (!sub) {
      if (sub.error() == ASTL_STATUS_NOT_IMPLEMENTED) {
        continue;
      }
      return std::unexpected(sub.error());
    }

    // Get units from the sensor feature type (libsensors provides temps in °C, power in W)
    astl_units_t units = ParseUnitsFromFeatureType(feature->type);
    if (units == ASTL_UNITS_UNKNOWN) {
      ASTL_LOG_WARNING("Unrecognized units for sensor feature type {}, skipping this feature.", feature->type);
      continue;
    }
    discovered_sensors.push_back(DiscoveredSensorMetric{
        .chip        = chip,
        .feature     = feature,
        .subfeature  = *sub,
        .chip_name   = chip_name.data(),
        .label       = label,
        .units       = units,
        .description = std::nullopt,
    });
    if (!HasUsableInitialValue(discovered_sensors.back(), sensors_api)) {
      discovered_sensors.pop_back();
      continue;
    }
    if (feature->type == SENSORS_FEATURE_TEMP) {
      AddOptionalTemperatureLimitMetrics(discovered_sensors, chip, feature, chip_name.data(), label, sensors_api);
    }
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
  auto              details_or_error =
      ResolveMetricRegistrationDetails(sensor, config_metric_name, target_context_iter->second.declarations);
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
      std::move(details.formula), ASTL_VALUE_FLOAT64, std::move(details.metric_groups));

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
#if defined(ASTL_INCLUDE_LIBSENSORS)
  auto libsensors_targets_iter = collector_type_to_targets_map.find(CollectorType::LIBSENSORS);
  if (libsensors_targets_iter == collector_type_to_targets_map.end()) {
    ASTL_LOG_INFO("No targets with LIBSENSORS collector type found, skipping LIBSENSORS metric registration");
    return ASTL_STATUS_SUCCESS;
  }
  const auto& libsensors_targets = libsensors_targets_iter->second;
  if (libsensors_targets.empty()) {
    ASTL_LOG_INFO("LIBSENSORS collector type present with no targets, skipping LIBSENSORS metric registration");
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
        LibsensorsTargetContext{.target = libsensors_target, .declarations = std::move(declarations_or_error.value())});
  }

  const sensors_chip_name*            chip       = nullptr;
  int                                 chip_index = 0;
  std::vector<DiscoveredSensorMetric> discovered_sensors;
  while ((chip = sensors_api->get_detected_chips(nullptr, &chip_index))) {
    auto discovered_or_error = DiscoverSensorsFromChip(configuration, chip, sensors_api.get());
    if (!discovered_or_error.has_value()) {
      return discovered_or_error.error();
    }
    auto& chip_sensors = discovered_or_error.value();
    discovered_sensors.insert(discovered_sensors.end(), std::make_move_iterator(chip_sensors.begin()),
                              std::make_move_iterator(chip_sensors.end()));
  }

  std::unordered_map<std::string, int> chip_label_counts;
  for (const auto& sensor : discovered_sensors) {
    ++chip_label_counts[sensor.chip_name + " " + sensor.label];
  }

  for (const auto& sensor : discovered_sensors) {
    auto status = RegisterSensorMetric(sensor, chip_label_counts, metric_manager, target_contexts);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
#else
  (void)configuration;
  (void)collector_type_to_targets_map;
  (void)metric_manager;
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
