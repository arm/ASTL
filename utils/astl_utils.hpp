// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_ASTL_UTILS_HPP_
#define INCLUDE_ASTL_UTILS_HPP_

#include <algorithm>
#include <magic_enum/magic_enum.hpp>
#include <random>
#include <string>
#include <string_view>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"

namespace astl {

/**
 * @brief Enum of all environment variables used by ASTL
 *
 * This provides a constrained set of environment variables that can be accessed,
 * making it clear which variables are used throughout the codebase.
 */
enum class EnvVar {

  /* optional override for scmi sysfs telemetry root path */
  ASTL_SCMI_SYSFS_TELEMETRY_ROOT,

  /* optional override for astl config directory */
  ASTL_CONFIG_DIR,

  /* selects the output file path for Perfetto format output. If not set, no output is created. */
  ASTL_OUTPUT_PERFETTO,

  /* selects the output path for interval CSV output. If not set, no output is created. */
  ASTL_OUTPUT_INTERVAL_CSV,

  /* selects the output path for metric summary CSV output. If not set, no output is created. */
  ASTL_OUTPUT_SUMMARY_CSV,

  /* Environment variable used to override log level
   * Value should be set to string "trace", "debug", "info", "warn", "error", "critical", "default" or "off"
   * upper case and first letter uppercase for each of the expected values are acceptable alternatives
   */
  ASTL_LOG_LEVEL,

  /* Environment variable used to override the log file name
   */
  ASTL_LOG_NAME,

  /* Environment variable used to override logging to console.
   * It is used to enable logging to the console when the compiled in value is set to off
   * It cannot be used to turn off logging to the console if the code explicitely enables logging to the console
   * Any value would activate the variable other than explicit negative values: 0, no, off or empty
   */
  ASTL_LOG_CONSOLE,

  /* Environment variable used to enable adding source location to the formatted log messages
   * Any non negative value would activate the variable
   */
  ASTL_LOG_SOURCE_LOC,

  /* used by file interface to expand ~ */
  HOME,

  /* used to look up windows-specific application data path for config dir */
  LOCALAPPDATA,

  /* used to look up windows-specific application data path for config dir */
  PROGRAMDATA,

  /* used to look up user-specific app data path for Linux */
  XDG_DATA_HOME,

  /* used by tests to know if running under sudo */
  SUDO_UID,

  /* used by tests to force SCMI process-lock temp dir lookup failure */
  ASTL_TEST_FORCE_SCMI_PROCESS_LOCK_TEMP_DIR_FAILURE,
};

/**
 * @brief Get the string name of an environment variable
 *
 * @param env_var The environment variable enum value
 * @return The string name of the environment variable
 */
inline std::string_view GetEnvVarName(EnvVar env_var) { return magic_enum::enum_name(env_var); }

/* @brief Set specified environment variable to the specified value
 *
 * @param env_var   The environment variable to set
 * @param var_value The value to set the environment variable to
 *
 * @return astl_status_code
 */
inline astl_status_code SetEnvVar(EnvVar env_var, const std::string& var_value) {
  std::string      var_name = std::string(GetEnvVarName(env_var));
  astl_status_code status   = ASTL_STATUS_INTERNAL_ERROR;
#ifndef _WIN32  // Linux
  status = (setenv(var_name.c_str(), var_value.c_str(), 1) != 0) ? status : ASTL_STATUS_SUCCESS;
#else  // Windows
  std::string env_value = var_name + "=" + var_value;
  status                = (_putenv(env_value.c_str()) != 0) ? status : ASTL_STATUS_SUCCESS;
#endif

  return status;
}

/* @brief Get environment variable value
 * @param env_var  The environment variable enum value
 *
 * @return the value of the environment variable if found or empty std::string()
 */
inline std::string GetEnvVar(EnvVar env_var) {
  std::string var_name = std::string(GetEnvVarName(env_var));
#ifndef _WIN32  // Linux
  const char* var = getenv(var_name.c_str());
  return (var == nullptr) ? std::string() : std::string(var);
#else  // Windows
  char* var_value = nullptr;
  if (_dupenv_s(&var_value, nullptr, var_name.c_str()) != 0 || var_value == nullptr) {
    return std::string();
  }
  std::string var(var_value);
  free(var_value);
  return var;
#endif
}

/* @brief Check if an environment variable is set
 *
 * @param env_var  The environment variable enum value
 *
 * @return true if environment variable is set, false otherwise
 * Note: An environment variable is considered set if it is present and it is set to
 * any value other than empty string, 0, off, no, false or their uppercase version
 */
inline bool IsEnvVarSet(EnvVar env_var) {
  std::string var = GetEnvVar(env_var);
  std::transform(var.begin(), var.end(), var.begin(), [](unsigned char character) { return std::toupper(character); });
  bool var_set = true;
  if (var.empty() || var == "0" || var == "OFF" || var == "NO" || var == "FALSE") {
    var_set = false;
  }

  // Environment variable is set but not set to any of the expected off values
  return var_set;
}

/* @brief Generate a random number
 *
 * @return uint64_t random value
 */
inline uint64_t GetRandomNumber() {
  static std::mt19937                            prng(std::random_device{}());  // pseudorandom number generator
  static std::uniform_int_distribution<uint64_t> rand_num(0);                   // random number
  return rand_num(prng);
}

inline std::string ToLowerCopy(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(), [](unsigned char letter) { return std::tolower(letter); });
  return str;
}

inline std::string ToLowerCopy(std::string_view str) {
  std::string out_str(str);
  std::transform(str.begin(), str.end(), out_str.begin(), [](unsigned char letter) { return std::tolower(letter); });
  return out_str;
}

inline auto ParseMetricIdentifier(std::string const& identifier_str) -> astl_metric_identifier_t {
  auto identifier_str_lower = ToLowerCopy(identifier_str);
  if (identifier_str_lower == "count") {
    return ASTL_METRIC_IDENTIFIER_COUNT;
  }
  if (identifier_str_lower == "fan_speed" || identifier_str_lower == "fan speed" ||
      identifier_str_lower == "fanspeed") {
    return ASTL_METRIC_IDENTIFIER_FAN_SPEED;
  }
  if (identifier_str_lower == "temperature") {
    return ASTL_METRIC_IDENTIFIER_TEMPERATURE;
  }
  if (identifier_str_lower == "thermal_throttle" || identifier_str_lower == "thermal throttle" ||
      identifier_str_lower == "thermalthrottle") {
    return ASTL_METRIC_IDENTIFIER_THERMAL_THROTTLE;
  }
  if (identifier_str_lower == "energy") {
    return ASTL_METRIC_IDENTIFIER_ENERGY;
  }
  if (identifier_str_lower == "power") {
    return ASTL_METRIC_IDENTIFIER_POWER;
  }
  if (identifier_str_lower == "power_limit" || identifier_str_lower == "power limit" ||
      identifier_str_lower == "powerlimit") {
    return ASTL_METRIC_IDENTIFIER_POWER_LIMIT;
  }
  if (identifier_str_lower == "power_throttle" || identifier_str_lower == "power throttle" ||
      identifier_str_lower == "powerthrottle") {
    return ASTL_METRIC_IDENTIFIER_POWER_THROTTLE;
  }
  if (identifier_str_lower == "frequency") {
    return ASTL_METRIC_IDENTIFIER_FREQUENCY;
  }
  if (identifier_str_lower == "voltage") {
    return ASTL_METRIC_IDENTIFIER_VOLTAGE;
  }
  if (identifier_str_lower == "current") {
    return ASTL_METRIC_IDENTIFIER_CURRENT;
  }
  if (identifier_str_lower == "bandwidth") {
    return ASTL_METRIC_IDENTIFIER_BANDWIDTH;
  }
  if (identifier_str_lower == "humidity") {
    return ASTL_METRIC_IDENTIFIER_HUMIDITY;
  }
  if (identifier_str_lower == "thermal_limit" || identifier_str_lower == "thermal limit" ||
      identifier_str_lower == "thermallimit") {
    return ASTL_METRIC_IDENTIFIER_THERMAL_LIMIT;
  }
  if (identifier_str_lower == "status" || identifier_str_lower == "state") {
    return ASTL_METRIC_IDENTIFIER_STATUS;
  }
  return ASTL_METRIC_IDENTIFIER_UNKNOWN;
}

inline auto ParseUnits(std::string_view units_str) -> astl_units_t {
  auto unit_str_lower = ToLowerCopy(units_str);
  if (unit_str_lower == "none" || unit_str_lower.empty()) {
    return ASTL_UNITS_NONE;
  }
  if (unit_str_lower == "ticks") {
    return ASTL_UNITS_TICKS;
  }
  if (unit_str_lower == "s" || unit_str_lower == "sec" || unit_str_lower == "second" || unit_str_lower == "seconds") {
    return ASTL_UNITS_SECONDS;
  }
  if (unit_str_lower == "c" || unit_str_lower == "celsius" || unit_str_lower == "celcius") {
    return ASTL_UNITS_CELSIUS;
  }
  if (unit_str_lower == "j" || unit_str_lower == "joule" || unit_str_lower == "joules") {
    return ASTL_UNITS_JOULES;
  }
  if (unit_str_lower == "w" || unit_str_lower == "watt" || unit_str_lower == "watts") {
    return ASTL_UNITS_WATTS;
  }
  if (unit_str_lower == "v" || unit_str_lower == "volt" || unit_str_lower == "volts") {
    return ASTL_UNITS_VOLTS;
  }
  if (unit_str_lower == "a" || unit_str_lower == "amp" || unit_str_lower == "amps") {
    return ASTL_UNITS_AMPS;
  }
  if (unit_str_lower == "b" || unit_str_lower == "byte" || unit_str_lower == "bytes") {
    return ASTL_UNITS_BYTES;
  }
  if (unit_str_lower == "mbps" || unit_str_lower == "mb/s") {
    return ASTL_UNITS_MBYTESPERSEC;
  }
  if (unit_str_lower == "mhz") {
    return ASTL_UNITS_MHERTZ;
  }
  if (unit_str_lower == "rpm") {
    return ASTL_UNITS_RPM;
  }
  if (unit_str_lower == "count" || unit_str_lower == "counts") {
    return ASTL_UNITS_COUNT;
  }
  if (unit_str_lower == "%" || unit_str_lower == "percent" || unit_str_lower == "percentage") {
    return ASTL_UNITS_PERCENT;
  }
  return ASTL_UNITS_UNKNOWN;
}

// For user-facing labels, the switch is the better fit than magic_enum as it allows for
// more control over the formatting of the returned string
inline auto UnitsToString(astl_units_t units) -> std::string_view {
  switch (units) {
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
    case ASTL_UNITS_RPM:
      return "RPM";
    case ASTL_UNITS_COUNT:
      return "Count";
    case ASTL_UNITS_PERCENT:
      return "Percent";
    case ASTL_UNITS_UNKNOWN:
      return "Unknown";
  }
  return "Unknown";
}

inline auto ParseMetricType(std::string const& metric_type_str) -> astl_metric_type_t {
  auto metric_type_lower = ToLowerCopy(metric_type_str);
  if (metric_type_lower == "val" || metric_type_lower == "value") {
    return ASTL_METRIC_VALUE;
  }
  if (metric_type_lower == "set" || metric_type_lower == "finite" || metric_type_lower == "finite_set") {
    return ASTL_METRIC_FINITE_SET_VALUE;
  }
  if (metric_type_lower == "e" || metric_type_lower == "event") {
    return ASTL_METRIC_EVENT;
  }
  if (metric_type_lower == "d" || metric_type_lower == "delta") {
    return ASTL_METRIC_DELTA;
  }
  if (metric_type_lower == "residency") {
    return ASTL_METRIC_RESIDENCY;
  }
  if (metric_type_lower == "r" || metric_type_lower == "rate") {
    return ASTL_METRIC_RATE;
  }
  return ASTL_METRIC_UNKNOWN;
}

}  // namespace astl

namespace std {
inline auto to_string(astl_units_t units) -> std::string {
  std::string_view name = magic_enum::enum_name(units);
  // ignore the first part of the name, which is "ASTL_UNITS_";
  constexpr size_t prefix_length = 11;  // length of "ASTL_UNITS_";
  return name.empty() ? "UNKNOWN" : name.data() + prefix_length;
}
}  // namespace std

#endif /* INCLUDE_ASTL_UTILS_HPP_ */
