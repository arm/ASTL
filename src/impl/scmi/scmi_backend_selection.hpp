// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_BACKEND_SELECTION_HPP_
#define SCMI_BACKEND_SELECTION_HPP_

#include <algorithm>
#include <cctype>
#include <string>

#include "astl_logger.hpp"
#include "astl_utils.hpp"

namespace astl {

/**
 * @brief User-selected SCMI telemetry backend preference.
 *
 * The preference is derived from ASTL_SCMI_INTERFACE and is used by topology,
 * metric availability, and collector construction to consistently choose
 * between the legacy sysfs interface and the SCMI telemetry ioctl interface.
 */
enum class ScmiBackendPreference {
  /** @brief Prefer ioctl when it is available, otherwise fall back to sysfs. */
  AUTO,

  /** @brief Force use of the SCMI telemetry ioctl interface. */
  IOCTL,

  /** @brief Force use of the legacy SCMI telemetry sysfs interface. */
  SYSFS,
};

/**
 * @brief Reads ASTL_SCMI_INTERFACE and converts it into a backend preference.
 *
 * Accepted values are "auto", "ioctl", "iocl", and "sysfs". An unset variable
 * or unknown value resolves to ScmiBackendPreference::AUTO.
 *
 * @return The SCMI backend preference requested by the environment.
 */
inline auto GetScmiBackendPreference() -> ScmiBackendPreference {
  std::string value = GetEnvVar(EnvVar::ASTL_SCMI_INTERFACE);
  std::ranges::transform(value, value.begin(),
                         [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

  if (value.empty() || value == "auto") {
    return ScmiBackendPreference::AUTO;
  }
  if (value == "ioctl" || value == "iocl") {
    return ScmiBackendPreference::IOCTL;
  }
  if (value == "sysfs") {
    return ScmiBackendPreference::SYSFS;
  }

  ASTL_LOG_WARNING("Unknown ASTL_SCMI_INTERFACE value '{}'; using auto", value);
  return ScmiBackendPreference::AUTO;
}

/**
 * @brief Checks whether a preference permits probing or using ioctl.
 *
 * @param preference The SCMI backend preference to evaluate.
 * @return true when ioctl is allowed for the given preference.
 */
inline auto ScmiPreferenceAllowsIoctl(ScmiBackendPreference preference) -> bool {
  return preference == ScmiBackendPreference::AUTO || preference == ScmiBackendPreference::IOCTL;
}

/**
 * @brief Checks whether a preference permits probing or using sysfs.
 *
 * @param preference The SCMI backend preference to evaluate.
 * @return true when sysfs is allowed for the given preference.
 */
inline auto ScmiPreferenceAllowsSysfs(ScmiBackendPreference preference) -> bool {
  return preference == ScmiBackendPreference::AUTO || preference == ScmiBackendPreference::SYSFS;
}

}  // namespace astl

#endif  // SCMI_BACKEND_SELECTION_HPP_
