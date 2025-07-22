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

#ifndef STATIC_METRIC_CONFIG_HPP_
#define STATIC_METRIC_CONFIG_HPP_

#include "astl/astl_telemetry.h"
#include "common/capabilities.hpp"
#include "metric/metric_config.hpp"

namespace astl {

inline const std::vector<std::string> kDataEventIds = {"0x1234"};

inline const MetricConfig kTemperature{
    "SoC Temperature", "SoC Temperature in Celsius", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64,
    ASTL_METRIC_VALUE, CollectorType::SCMI,          kDataEventIds};

inline const MetricConfig kThermalCount{"Throttle Counts", "Number of thermal throttling events",
                                        ASTL_UNITS_NONE,   ASTL_VALUE_UINT64,
                                        ASTL_METRIC_DELTA, CollectorType::SCMI,
                                        kDataEventIds};

inline const MetricConfig kPower{"SoC Power",      "Current power consumption in watts",
                                 ASTL_UNITS_WATTS, ASTL_VALUE_UINT64,
                                 ASTL_METRIC_RATE, CollectorType::SCMI,
                                 kDataEventIds};

// Vector containing all metric configurations
inline const std::vector<MetricConfig> kMetricConfigs = {kTemperature, kThermalCount, kPower};

}  // namespace astl

#endif  // STATIC_METRIC_CONFIG_HPP_
