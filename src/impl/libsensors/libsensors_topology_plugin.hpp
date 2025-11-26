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

#ifndef LIBSENSORS_TOPOLOGY_PLUGIN_HPP_
#define LIBSENSORS_TOPOLOGY_PLUGIN_HPP_

#include <expected>
#include <memory>
#include <vector>

#ifdef ASTL_INCLUDE_LIBSENSORS
#  include "libsensors/libsensors_api.hpp"
#endif

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "config/astl_configuration.hpp"
#include "libsensors/libsensors_target.hpp"
#include "target.hpp"

namespace astl {

namespace LibsensorsTopologyPlugin {

namespace detail {

#ifdef ASTL_INCLUDE_LIBSENSORS
/**
 * @brief Returns a list of targets accessible via lm-sensors on this platform
 *
 * @param configuration The ASTL configuration
 */
auto ScanForTargetsWithLibsensors(const AstlConfiguration&    configuration,
                                  std::shared_ptr<SensorsApi> sensors_api = nullptr)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  std::vector<std::unique_ptr<ITarget> > targets;
  (void)configuration;  // currently unused
  // dynamically load and initialize the libsensors library with default system configuration
  if (!sensors_api) {
    sensors_api = SensorsApi::Create();
  }
  if (!sensors_api) {
    // if we're unable to load libsensors library at all, that's a valid situation. Maybe
    // we have SCMI Sysfs or some other telemetry available.
    ASTL_LOG_WARNING(
        "LibsensorsTopologyPlugin::ScanForTargets: "
        "Unable to dynamically load libsensors library. Please install libsensors5 runtime library to access sensors");
    return {};
  }
  if (!sensors_api->Ok()) {
    // If we loaded the sensors library but some of the functions didn't load properly, treat that as an error
    ASTL_LOG_ERROR("LibsensorsTopologyPlugin::ScanForTargets: Failed to load all requisite functions from libsensors");
    return std::unexpected(ASTL_STATUS_NO_TARGETS_FOUND);
  }

  const sensors_chip_name* chip       = nullptr;
  int                      chip_index = 0;
  while ((chip = sensors_api->get_detected_chips(nullptr, &chip_index))) {
    const sensors_feature*            feature              = nullptr;
    int                               sensor_feature_count = 0;
    constexpr size_t                  max_name_length      = 200;
    std::array<char, max_name_length> chip_name{'\0'};
    sensors_api->snprintf_chip_name(chip_name.data(), max_name_length, chip);
    ASTL_LOG_DEBUG("Scanning {} for features", chip_name.data());
    while ((feature = sensors_api->get_features(chip, &sensor_feature_count))) {
      if (feature->type == SENSORS_FEATURE_TEMP) {
        ASTL_LOG_DEBUG("  Found temperature sensor: {}", feature->name);
      } else if (feature->type == SENSORS_FEATURE_FAN) {
        ASTL_LOG_DEBUG("  Found fan sensor: {}", feature->name);
      } else if (feature->type == SENSORS_FEATURE_IN) {
        ASTL_LOG_DEBUG("  Found voltage sensor: {}", feature->name);
      } else if (feature->type == SENSORS_FEATURE_POWER) {
        ASTL_LOG_DEBUG("  Found power sensor: {}", feature->name);
      } else if (feature->type == SENSORS_FEATURE_HUMIDITY) {
        ASTL_LOG_DEBUG("  Found humidity sensor: {}", feature->name);
      } else if (feature->type == SENSORS_FEATURE_VID) {
        ASTL_LOG_DEBUG("  Found VID sensor: {}", feature->name);
      } else {
        ASTL_LOG_DEBUG("  Found other sensor type {}: {}", feature->type, feature->name);
      }
    }
    // if we find any chips with features, create a target for them.
    // continue scanning for more chips, only so we can log their features for now.
    if (sensor_feature_count > 0 && targets.empty()) {
      targets.push_back(std::make_unique<LibsensorsTarget>(
          "libsensors", "Collection of sensors from libsensors library", sensors_api));
    }
  }
  return targets;
}
#endif

}  // namespace detail

/**
 * @brief Returns a list of targets accessible via Libsensors on this platform
 */
inline auto ScanForTargets(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::unique_ptr<ITarget> >, astl_status_code> {
  (void)configuration;  // currently unused
#ifndef ASTL_INCLUDE_LIBSENSORS
  ASTL_LOG_WARNING("LibsensorsTopologyPlugin::ScanForTargets: ASTL was not compiled with libsensors support");
  return {};
#else
  return detail::ScanForTargetsWithLibsensors(configuration);
#endif
}

}  // namespace LibsensorsTopologyPlugin

}  // namespace astl

#endif  // LIBSENSORS_TOPOLOGY_PLUGIN_HPP_
