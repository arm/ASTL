// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "libsensors/libsensors_metric_builder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_api.hpp"
#  include "libsensors/libsensors_target.hpp"
#endif

namespace astl {

#if defined(ASTL_INCLUDE_LIBSENSORS)

struct DiscoveredSensorMetric {
  const sensors_chip_name*  chip;
  const sensors_feature*    feature;
  const sensors_subfeature* subfeature;
  std::string               chip_name;
  std::string               label;
  astl_units_t              units;
};

static auto NormalizeNameComponent(std::string text) -> std::string {
  std::replace_if(
      text.begin(), text.end(), [](char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; },
      '_');
  return text;
}

static auto BuildChipLabelKey(const DiscoveredSensorMetric& sensor) -> std::string {
  return NormalizeNameComponent(sensor.chip_name) + "_" + sensor.label;
}

static auto BuildMetricDescription(const DiscoveredSensorMetric& sensor) -> std::string {
  switch (sensor.feature->type) {
    case SENSORS_FEATURE_TEMP:
      return "Temperature reading for " + sensor.label + " on chip " + sensor.chip_name;
    case SENSORS_FEATURE_POWER:
      return "Power reading for " + sensor.label + " on chip " + sensor.chip_name;
    case SENSORS_FEATURE_FAN:
      return "Fan speed reading for " + sensor.label + " on chip " + sensor.chip_name;
    default:
      return sensor.label + " reading on chip " + sensor.chip_name;
  }
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
  const sensors_subfeature* sub = sensors_api->get_subfeature(chip, feature, subtype);
  if (!sub || (sub->flags & SENSORS_MODE_R) == 0) {
    const auto label = GetOwnedSensorLabel(chip, feature, sensors_api);
    ASTL_LOG_WARNING("GetSubfeature: No valid input subfeature found for {} sensor: {}", feature->name,
                     label != nullptr ? label.get() : "<unknown>");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return sub;
}

/**
 * @brief Parse units from sensor feature type
 * libsensors provides temperature in degrees Celsius, power in Watts and fan speedn in RPM
 */
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
 * @brief Build a unique ASTL metric name for a discovered libsensors metric.
 */
static auto BuildMetricName(const DiscoveredSensorMetric&               sensor,
                            const std::unordered_map<std::string, int>& chip_label_counts) -> std::string {
  const std::string chip_qualified_name = BuildChipLabelKey(sensor);
  if (auto chip_label_iter = chip_label_counts.find(chip_qualified_name);
      chip_label_iter != chip_label_counts.end() && chip_label_iter->second == 1) {
    return chip_qualified_name;
  }

  return chip_qualified_name + "_" + std::to_string(sensor.feature->number);
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
        .chip       = chip,
        .feature    = feature,
        .subfeature = *sub,
        .chip_name  = chip_name.data(),
        .label      = label,
        .units      = units,
    });
    if (!HasUsableInitialValue(discovered_sensors.back(), sensors_api)) {
      discovered_sensors.pop_back();
    }
  }
  return discovered_sensors;
}

/**
 * @brief Register a discovered sensor using a conflict-free metric name.
 */
static auto RegisterSensorMetric(const DiscoveredSensorMetric&               sensor,
                                 const std::unordered_map<std::string, int>& chip_label_counts,
                                 IMetricManager* metric_manager, const std::vector<const ITarget*>& libsensors_targets)
    -> astl_status_code {
  const std::string           metric_name = BuildMetricName(sensor, chip_label_counts);
  const std::string           description = BuildMetricDescription(sensor);
  std::vector<const ITarget*> matching_targets;
  for (const auto* target : libsensors_targets) {
    const auto* libsensors_target = dynamic_cast<const LibsensorsTarget*>(target);
    if (!libsensors_target) {
      ASTL_LOG_ERROR("Libsensors target cannot be cast to LibsensorsTarget type");
      return ASTL_STATUS_BAD_CONFIGURATION;
    }
    if (libsensors_target->ChipName() == sensor.chip_name) {
      matching_targets.push_back(target);
    }
  }
  if (matching_targets.empty()) {
    ASTL_LOG_ERROR("No Libsensors target found for chip '{}'", sensor.chip_name);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  auto metric_config = std::make_unique<MetricConfig>(
      metric_name, description, sensor.units, ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      CollectorType::LIBSENSORS, LibsensorsOperationBuilder{sensor.chip, sensor.subfeature->number});

  return metric_manager->RegisterMetric(std::move(metric_config), matching_targets);
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
  (void)configuration;  // @todo(ASTL_139) use configuration to filter which sensors to include
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
    ++chip_label_counts[BuildChipLabelKey(sensor)];
  }

  for (const auto& sensor : discovered_sensors) {
    auto status = RegisterSensorMetric(sensor, chip_label_counts, metric_manager, libsensors_targets);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
#else
  (void)collector_type_to_targets_map;
  (void)metric_manager;
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
