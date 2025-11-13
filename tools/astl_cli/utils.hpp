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

#ifndef ASTL_CLI_UTILS_H
#define ASTL_CLI_UTILS_H

#include <string_view>

#include "astl/astl_telemetry.h"

inline auto UnitsToString(astl_units_t unit) -> std::string_view {
  switch (unit) {
    case ASTL_UNITS_NONE:
      return "None";
    case ASTL_UNITS_TICKS:
      return "Ticks";
    case ASTL_UNITS_SECONDS:
      return "Seconds";
    case ASTL_UNITS_CELSIUS:
      return "Celsius";
    case ASTL_UNITS_JOULES:
      return "Joules";
    case ASTL_UNITS_WATTS:
      return "Watts";
    case ASTL_UNITS_VOLTS:
      return "Volts";
    case ASTL_UNITS_AMPS:
      return "Amps";
    case ASTL_UNITS_BYTES:
      return "Bytes";
    case ASTL_UNITS_MBYTESPERSEC:
      return "MB/s";
    case ASTL_UNITS_MHERTZ:
      return "MHz";
    case ASTL_UNITS_UNKNOWN:
      return "Unknown";
  }
  return "Unknown";
}

inline auto MetricTypeToString(astl_metric_type_t type) -> std::string_view {
  switch (type) {
    case ASTL_METRIC_VALUE:
      return "Value";
    case ASTL_METRIC_FINITE_SET_VALUE:
      return "Finite Set Value";
    case ASTL_METRIC_EVENT:
      return "Event";
    case ASTL_METRIC_DELTA:
      return "Delta";
    case ASTL_METRIC_RESIDENCY:
      return "Residency";
    case ASTL_METRIC_RATE:
      return "Rate";
    case ASTL_METRIC_UNKNOWN:
      return "Unknown";
  }
  return "Unknown";
}

inline auto AstlValueAsDouble(const astl_value_t& value, astl_value_type_t value_type) -> double {
  switch (value_type) {
    case ASTL_VALUE_UINT8:
      return static_cast<double>(value.ui8);
    case ASTL_VALUE_UINT16:
      return static_cast<double>(value.ui16);
    case ASTL_VALUE_UINT32:
      return static_cast<double>(value.ui32);
    case ASTL_VALUE_UINT64:
      return static_cast<double>(value.ui64);
    case ASTL_VALUE_FLOAT32:
      return static_cast<double>(value.fp32);
    case ASTL_VALUE_FLOAT64:
      return value.fp64;
    case ASTL_VALUE_BOOL8:
      return value.b8 ? 1.0 : 0.0;
    case ASTL_VALUE_STRING:
      return 0.0;  // cannot convert string to double
    case ASTL_VALUE_UNKNOWN:
      return 0.0;
  }
  return 0.0;
}

#endif  // ASTL_CLI_UTILS_H