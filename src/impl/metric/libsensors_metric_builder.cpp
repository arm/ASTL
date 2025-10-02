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

auto GetSubfeature(const sensors_chip_name* chip, const sensors_feature* feature)
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
  const sensors_subfeature* sub = sensors_get_subfeature(chip, feature, subtype);
  if (!sub || (sub->flags & SENSORS_MODE_R) == 0) {
    ASTL_LOG_WARNING("GetSubfeature: No valid input subfeature found for {} sensor: {}", feature->name,
                     sensors_get_label(chip, feature));
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return sub;
}

/**
 * @brief Register sensors from a detected chip.
 */
static auto RegisterSensorsFromChip(const astl::AstlConfiguration& configuration, IMetricManager* metric_manager,
                                    const std::vector<const ITarget*>& libsensors_targets,
                                    const sensors_chip_name*           chip) -> astl_status_code {
  const sensors_feature*            feature              = nullptr;
  int                               sensor_feature_count = 0;
  constexpr size_t                  max_name_length      = 200;
  std::array<char, max_name_length> chip_name{'\0'};
  sensors_snprintf_chip_name(chip_name.data(), max_name_length, chip);
  ASTL_LOG_INFO("Scanning {} for features", chip_name.data());
  while ((feature = sensors_get_features(chip, &sensor_feature_count))) {
    ASTL_LOG_DEBUG("  Found sensor: {} type {} with name {}", sensors_get_label(chip, feature), feature->type,
                   feature->name);
    const auto sub = GetSubfeature(chip, feature);
    if (!sub) {
      if (sub.error() == ASTL_STATUS_NOT_IMPLEMENTED) {
        continue;
      }
      return sub.error();
    }
    const auto& metric_iter =
        std::find_if(configuration.metric_declarations.begin(), configuration.metric_declarations.end(),
                     [&](const auto& metric_declaration_iter) {
                       const auto& metric_declaration = metric_declaration_iter.second;
                       return (metric_declaration.collection_protocol == "libsensors" &&
                               metric_declaration.register_name == chip_name.data()) &&
                              (metric_declaration.offset == feature->name);
                     });
    if (metric_iter == configuration.metric_declarations.end()) {
      ASTL_LOG_INFO("Skipping sensor {} as not configured in ASTL configuration", sensors_get_label(chip, feature));
      continue;
    }
    const auto& [metric_name, metric_declaration] = *metric_iter;

    auto metric_config =
        std::make_unique<MetricConfig>(metric_name, feature->name, ParseUnits(metric_declaration), ASTL_VALUE_FLOAT64,
                                       ParseMetricType(metric_declaration), CollectorType::LIBSENSORS,
                                       LibsensorsOperationBuilder{chip, (*sub)->number});

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

  const sensors_chip_name* chip       = nullptr;
  int                      chip_index = 0;
  while ((chip = sensors_get_detected_chips(nullptr, &chip_index))) {
    auto status = RegisterSensorsFromChip(configuration, metric_manager, libsensors_targets, chip);
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
