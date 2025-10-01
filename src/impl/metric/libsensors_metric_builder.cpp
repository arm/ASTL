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

#include "metric/libsensors_metric_builder.hpp"

#include <unordered_map>
#include <vector>

#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"

#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include <sensors/sensors.h>
#endif

namespace astl {

#if defined(ASTL_INCLUDE_LIBSENSORS)
static auto RegisterTempSensor(IMetricManager* metric_manager, std::vector<const ITarget*> const& targets,
                               const sensors_chip_name* chip, const sensors_feature* feature) -> astl_status_code {
  const sensors_subfeature* sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_TEMP_INPUT);
  if (!sub || (sub->flags & SENSORS_MODE_R) == 0) {
    ASTL_LOG_WARNING("No valid input subfeature found for temperature sensor: {}", sensors_get_label(chip, feature));
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  auto metric_config = std::make_unique<MetricConfig>(
      sensors_get_label(chip, feature), "Temperature sensor", ASTL_UNITS_CELSIUS, ASTL_VALUE_FLOAT32, ASTL_METRIC_VALUE,
      CollectorType::LIBSENSORS, LibsensorsOperationBuilder{chip, sub->number});

  return metric_manager->RegisterMetric(std::move(metric_config), targets);
}

static auto RegisterPowerSensor(IMetricManager* metric_manager, std::vector<const ITarget*> const& targets,
                                const sensors_chip_name* chip, const sensors_feature* feature) -> astl_status_code {
  const sensors_subfeature* sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_POWER_INPUT);
  if (!sub || (sub->flags & SENSORS_MODE_R) == 0) {
    ASTL_LOG_WARNING("No valid input subfeature found for power sensor: {}", sensors_get_label(chip, feature));
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  LibsensorsOperationBuilder operation_builder{chip, sub->number};
  auto metric_config = std::make_unique<MetricConfig>(sensors_get_label(chip, feature), "Power sensor",
                                                      ASTL_UNITS_WATTS, ASTL_VALUE_FLOAT32, ASTL_METRIC_VALUE,
                                                      CollectorType::LIBSENSORS, std::move(operation_builder));

  return metric_manager->RegisterMetric(std::move(metric_config), targets);
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

  const sensors_chip_name* chip       = nullptr;
  int                      chip_index = 0;
  while ((chip = sensors_get_detected_chips(nullptr, &chip_index))) {
    const sensors_feature*            feature              = nullptr;
    int                               sensor_feature_count = 0;
    constexpr size_t                  max_name_length      = 200;
    std::array<char, max_name_length> chip_name{'\0'};
    sensors_snprintf_chip_name(chip_name.data(), max_name_length, chip);
    ASTL_LOG_INFO("Scanning {} for features", chip_name.data());
    // @todo(ASTL-187) Extend libsensors support with additional features/subfeatures
    // @todo(ASTL-139) match the chip/feature to the configuration to determine metric type, and whether to include
    while ((feature = sensors_get_features(chip, &sensor_feature_count))) {
      switch (feature->type) {
        case SENSORS_FEATURE_TEMP:
          RegisterTempSensor(metric_manager, libsensors_targets, chip, feature);
          break;
        case SENSORS_FEATURE_POWER:
          RegisterPowerSensor(metric_manager, libsensors_targets, chip, feature);
          break;
        default:
          ASTL_LOG_WARNING("Skipping unrecognized feature: {}", sensors_get_label(chip, feature));
      }
    }
  }
#else
  (void)collector_type_to_targets_map;
  (void)metric_manager;
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
