// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_ASTL_UTILS_HPP_
#define INCLUDE_ASTL_UTILS_HPP_

#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_magic_enum.hpp"

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

  /** @brief Optional override for the SCMI ioctl device root path. */
  ASTL_SCMI_IOCTL_DEV_ROOT,

  /** @brief Optional SCMI interface selection. Accepted values are auto, ioctl, and sysfs. */
  ASTL_SCMI_INTERFACE,

  /** @brief Optional comma-separated allowlist of collectors used for live discovery. */
  ASTL_COLLECTORS,

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

  /* set to any value other than empty, 0, off, no, or false to enable per-sample CSV logging to raw_samples.csv (debug
     feature) */
  ASTL_LOG_RAW_SAMPLES,

  /* set to any value other than empty, "0", "off", "no", or "false" (case-insensitive) to make the SCMI collector
   * use software clock (CLOCK_MONOTONIC_RAW) timestamps instead of the SCMI hardware counter.
   * When enabled, tstamp_enable is not written to sysfs at all.
   * Defaults to off (hardware counter). Useful when hardware timestamps are unavailable or unreliable. */
  ASTL_SCMI_USE_SOFTWARE_CLOCK_TIMESTAMPS,
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
  status = (_putenv_s(var_name.c_str(), var_value.c_str()) != 0) ? status : ASTL_STATUS_SUCCESS;
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
  if (unit_str_lower.empty()) {
    return ASTL_UNITS_NONE;
  }

  struct UnitAlias {
    std::string_view alias;
    astl_units_t     units;
  };

  static constexpr auto k_unit_aliases = std::to_array<UnitAlias>({
      {"none",       ASTL_UNITS_NONE        },
      {"ticks",      ASTL_UNITS_TICKS       },
      {"s",          ASTL_UNITS_SECONDS     },
      {"sec",        ASTL_UNITS_SECONDS     },
      {"second",     ASTL_UNITS_SECONDS     },
      {"seconds",    ASTL_UNITS_SECONDS     },
      {"c",          ASTL_UNITS_CELSIUS     },
      {"celsius",    ASTL_UNITS_CELSIUS     },
      {"celcius",    ASTL_UNITS_CELSIUS     },
      {"j",          ASTL_UNITS_JOULES      },
      {"joule",      ASTL_UNITS_JOULES      },
      {"joules",     ASTL_UNITS_JOULES      },
      {"w",          ASTL_UNITS_WATTS       },
      {"watt",       ASTL_UNITS_WATTS       },
      {"watts",      ASTL_UNITS_WATTS       },
      {"v",          ASTL_UNITS_VOLTS       },
      {"volt",       ASTL_UNITS_VOLTS       },
      {"volts",      ASTL_UNITS_VOLTS       },
      {"a",          ASTL_UNITS_AMPS        },
      {"amp",        ASTL_UNITS_AMPS        },
      {"amps",       ASTL_UNITS_AMPS        },
      {"b",          ASTL_UNITS_BYTES       },
      {"byte",       ASTL_UNITS_BYTES       },
      {"bytes",      ASTL_UNITS_BYTES       },
      {"mbps",       ASTL_UNITS_MBYTESPERSEC},
      {"mb/s",       ASTL_UNITS_MBYTESPERSEC},
      {"mhz",        ASTL_UNITS_MHZ         },
      {"%",          ASTL_UNITS_PERCENT     },
      {"percent",    ASTL_UNITS_PERCENT     },
      {"percentage", ASTL_UNITS_PERCENT     },
      {"rpm",        ASTL_UNITS_RPM         },
      {"count",      ASTL_UNITS_COUNT       },
      {"counts",     ASTL_UNITS_COUNT       },
  });

  const std::array<UnitAlias, std::size(k_unit_aliases)>::const_iterator alias_it = std::ranges::find_if(
      k_unit_aliases, [&unit_str_lower](const UnitAlias& alias) { return alias.alias == unit_str_lower; });
  if (alias_it != k_unit_aliases.end()) {
    return alias_it->units;
  }
  return ASTL_UNITS_UNKNOWN;
}

// For user-facing labels, the switch is the better fit than magic_enum as it allows for
// more control over the formatting of the returned string
inline auto UnitsToString(astl_units_t units) -> std::string_view {
  std::string_view units_text = "Unknown";
  switch (units) {
    case ASTL_UNITS_NONE:
      units_text = "None";
      break;
    case ASTL_UNITS_TICKS:
      units_text = "Ticks";
      break;
    case ASTL_UNITS_SECONDS:
      units_text = "Seconds";
      break;
    case ASTL_UNITS_CELSIUS:
      units_text = "Celsius";
      break;
    case ASTL_UNITS_JOULES:
      units_text = "Joules";
      break;
    case ASTL_UNITS_WATTS:
      units_text = "Watts";
      break;
    case ASTL_UNITS_VOLTS:
      units_text = "Volts";
      break;
    case ASTL_UNITS_AMPS:
      units_text = "Amps";
      break;
    case ASTL_UNITS_BYTES:
      units_text = "Bytes";
      break;
    case ASTL_UNITS_MBYTESPERSEC:
      units_text = "MB/s";
      break;
    case ASTL_UNITS_MHZ:
      units_text = "MHz";
      break;
    case ASTL_UNITS_PERCENT:
      units_text = "Percent";
      break;
    case ASTL_UNITS_RPM:
      units_text = "RPM";
      break;
    case ASTL_UNITS_COUNT:
      units_text = "Count";
      break;
    case ASTL_UNITS_UNKNOWN:
      break;
  }
  return units_text;
}

/**
 * @brief Compare the digit runs starting at @p lhs[left_index] and @p rhs[right_index].
 *
 * Leading zeros are ignored so numeric magnitude drives the comparison. Both
 * indices are advanced past their entire digit runs (including any leading
 * zeros). Returns a negative, zero, or positive value mirroring the numeric
 * ordering of the two runs.
 *
 * @param lhs          Left-hand string.
 * @param left_index   In/out cursor into @p lhs; advanced past the digit run.
 * @param rhs          Right-hand string.
 * @param right_index  In/out cursor into @p rhs; advanced past the digit run.
 * @return Negative if the left run is smaller, positive if larger, zero if equal.
 */
inline auto CompareDigitRuns(std::string_view lhs, std::size_t& left_index, std::string_view rhs,
                             std::size_t& right_index) -> int {
  const auto is_digit = [](char character) { return character >= '0' && character <= '9'; };

  // Skip leading zeros so numeric magnitude drives the comparison.
  std::size_t left_start = left_index;
  while (left_start < lhs.size() && lhs[left_start] == '0') {
    ++left_start;
  }
  std::size_t right_start = right_index;
  while (right_start < rhs.size() && rhs[right_start] == '0') {
    ++right_start;
  }

  std::size_t left_end = left_start;
  while (left_end < lhs.size() && is_digit(lhs[left_end])) {
    ++left_end;
  }
  std::size_t right_end = right_start;
  while (right_end < rhs.size() && is_digit(rhs[right_end])) {
    ++right_end;
  }

  // Advance the caller's cursors past both digit runs (including any zeros).
  while (left_index < lhs.size() && is_digit(lhs[left_index])) {
    ++left_index;
  }
  while (right_index < rhs.size() && is_digit(rhs[right_index])) {
    ++right_index;
  }

  const std::size_t left_len  = left_end - left_start;
  const std::size_t right_len = right_end - right_start;
  if (left_len != right_len) {
    return left_len < right_len ? -1 : 1;
  }
  return lhs.compare(left_start, left_len, rhs, right_start, right_len);
}

/**
 * @brief Compare two strings using natural (human) ordering.
 *
 * Splits each string into runs of digits and non-digits. Digit runs are
 * compared by numeric value (ignoring leading zeros) so that, for example,
 * "core 2" sorts before "core 10". Non-digit runs are compared
 * lexicographically. This avoids the purely lexicographic ordering that would
 * otherwise place "core 10" and "core 100" before "core 2".
 *
 * @param lhs  Left-hand string.
 * @param rhs  Right-hand string.
 * @return true if @p lhs should be ordered before @p rhs.
 */
inline auto NaturalLess(std::string_view lhs, std::string_view rhs) -> bool {
  const auto is_digit = [](char character) { return character >= '0' && character <= '9'; };

  std::size_t left_index  = 0;
  std::size_t right_index = 0;
  while (left_index < lhs.size() && right_index < rhs.size()) {
    const char left_char  = lhs[left_index];
    const char right_char = rhs[right_index];

    if (is_digit(left_char) && is_digit(right_char)) {
      if (const int digit_cmp = CompareDigitRuns(lhs, left_index, rhs, right_index); digit_cmp != 0) {
        return digit_cmp < 0;
      }
      continue;
    }

    if (left_char != right_char) {
      return left_char < right_char;
    }
    ++left_index;
    ++right_index;
  }

  // The loop exits once at least one string is exhausted; the shorter remaining
  // suffix sorts first (e.g. "core" before "core 1").
  return left_index >= lhs.size() && right_index < rhs.size();
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

inline auto to_string(astl_units_t units) -> std::string {
  std::string_view name = magic_enum::enum_name(units);
  // ignore the first part of the name, which is "ASTL_UNITS_";
  constexpr size_t prefix_length = 11;  // length of "ASTL_UNITS_";
  return name.empty() ? "UNKNOWN" : std::string(name.substr(prefix_length));
}
}  // namespace astl

#endif /* INCLUDE_ASTL_UTILS_HPP_ */
