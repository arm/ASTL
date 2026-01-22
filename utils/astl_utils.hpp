#ifndef INCLUDE_ASTL_UTILS_HPP_
#define INCLUDE_ASTL_UTILS_HPP_

#include <algorithm>
#include <magic_enum/magic_enum.hpp>
#include <random>
#include <string>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"

namespace astl {

/* @brief Set specified environment variable to the specified value
 *
 * @param var_name  The name of the environment variable to set
 * @param var_value The value to set the environment variable to
 *
 * @return astl_status_code
 */
inline astl_status_code SetEnvVar(const std::string& var_name, const std::string& var_value) {
  astl_status_code status = ASTL_STATUS_INTERNAL_ERROR;
  if (var_name.empty()) {
    return status;
  }
#ifndef _WIN32  // Linux
  status = (setenv(var_name.c_str(), var_value.c_str(), 1) != 0) ? status : ASTL_STATUS_SUCCESS;
#else  // Windows
  std::string env_value = var_name + "=" + var_value;
  status                = (_putenv(env_value.c_str()) != 0) ? status : ASTL_STATUS_SUCCESS;
#endif

  return status;
}

/* @brief Get environment variable value
 * @param var_name  The environment variable name
 *
 * @return the value of the environment variable if found or empty std::string()
 */
inline std::string GetEnvVar(const std::string& var_name) {
  if (var_name.empty()) {
    return std::string();
  }
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
 * @prarm var_name  The environment variable name
 *
 * @return true if environment variable is set, false otherwise
 * Note: An environment variable is considered set if it is present and it is set to
 * any value other than empty string, 0, off, no, false or their uppercase version
 */
inline bool IsEnvVarSet(const std::string& var_name) {
  std::string var = GetEnvVar(var_name);
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

inline auto ParseCategory(std::string const& category_str) -> astl_category_t {
  auto category_str_lower = ToLowerCopy(category_str);
  if (category_str_lower == "count") {
    return ASTL_CATEGORY_COUNT;
  }
  if (category_str_lower == "temperature") {
    return ASTL_CATEGORY_TEMPERATURE;
  }
  if (category_str_lower == "power") {
    return ASTL_CATEGORY_POWER;
  }
  if (category_str_lower == "frequency") {
    return ASTL_CATEGORY_FREQUENCY;
  }
  if (category_str_lower == "voltage") {
    return ASTL_CATEGORY_VOLTAGE;
  }
  if (category_str_lower == "current") {
    return ASTL_CATEGORY_CURRENT;
  }
  return ASTL_CATEGORY_UNCATEGORIZED;
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
  return ASTL_UNITS_UNKNOWN;
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
