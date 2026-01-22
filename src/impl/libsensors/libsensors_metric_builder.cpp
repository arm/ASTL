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

#include "libsensors/libsensors_metric_builder.hpp"

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
    default:
      ASTL_LOG_WARNING("GetSubfeature: Unrecognized feature type {}", feature->type);
      return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
  const sensors_subfeature* sub = sensors_api->get_subfeature(chip, feature, subtype);
  if (!sub || (sub->flags & SENSORS_MODE_R) == 0) {
    ASTL_LOG_WARNING("GetSubfeature: No valid input subfeature found for {} sensor: {}", feature->name,
                     sensors_api->get_label(chip, feature));
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return sub;
}

/**
 * @brief Parse units from sensor feature type
 * libsensors provides temperature in degrees Celsius and power in Watts
 */
static auto ParseUnitsFromFeatureType(sensors_feature_type feature_type) -> astl_units_t {
  switch (feature_type) {
    case SENSORS_FEATURE_TEMP:
      return ASTL_UNITS_CELSIUS;
    case SENSORS_FEATURE_POWER:
      return ASTL_UNITS_WATTS;
    default:
      return ASTL_UNITS_UNKNOWN;
  }
}

/**
 * @brief Register sensors from a detected chip.
 */
static auto RegisterSensorsFromChip(const astl::AstlConfiguration& configuration, IMetricManager* metric_manager,
                                    const std::vector<const ITarget*>& libsensors_targets,
                                    const sensors_chip_name* chip, SensorsApi const* sensors_api) -> astl_status_code {
  (void)configuration;
  const sensors_feature*            feature              = nullptr;
  int                               sensor_feature_count = 0;
  constexpr size_t                  max_name_length      = 200;
  std::array<char, max_name_length> chip_name{'\0'};
  sensors_api->snprintf_chip_name(chip_name.data(), max_name_length, chip);
  ASTL_LOG_INFO("Scanning {} for features", chip_name.data());
  while ((feature = sensors_api->get_features(chip, &sensor_feature_count))) {
    const char* label = sensors_api->get_label(chip, feature);
    ASTL_LOG_DEBUG("  Found sensor: `{}` type {} with name {}", label, feature->type, feature->name);
    const auto sub = GetSubfeature(chip, feature, sensors_api);
    if (!sub) {
      if (sub.error() == ASTL_STATUS_NOT_IMPLEMENTED) {
        continue;
      }
      return sub.error();
    }

    // Get units from the sensor feature type (libsensors provides temps in °C, power in W)
    astl_units_t units = ParseUnitsFromFeatureType(feature->type);
    if (units == ASTL_UNITS_UNKNOWN) {
      ASTL_LOG_WARNING("Unrecognized units for sensor feature type {}, skipping this feature.", feature->type);
      continue;
    }

    std::string description =
        std::string("Libsensors sensors ") + std::string(chip_name.data()) + " " + std::string(label);

    auto metric_config = std::make_unique<MetricConfig>(
        label, description, units, ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
        CollectorType::LIBSENSORS, LibsensorsOperationBuilder{chip, (*sub)->number});

    auto status = metric_manager->RegisterMetric(std::move(metric_config), libsensors_targets);
    if (ASTL_STATUS_SUCCESS != status) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
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

  const sensors_chip_name* chip       = nullptr;
  int                      chip_index = 0;
  while ((chip = sensors_api->get_detected_chips(nullptr, &chip_index))) {
    auto status = RegisterSensorsFromChip(configuration, metric_manager, libsensors_targets, chip, sensors_api.get());
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
