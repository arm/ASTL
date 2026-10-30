// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_CONSTANTS_HPP_
#define ASTL_CONSTANTS_HPP_

#include <chrono>
#include <string_view>

namespace astl {

/** @brief Default root of the legacy SCMI telemetry sysfs interface. */
constexpr std::string_view kDefaultScmiSysfsTelemetryRootPath = "/sys/fs/arm_telemetry";

/** @brief Default root containing SCMI telemetry ioctl character devices. */
constexpr std::string_view kDefaultScmiIoctlDeviceRootPath = "/dev/scmi";

// the rate in KHz that a timestamp increments for a given data event, used to interpret timestamp values
using kilohertz = uint32_t;

}  // namespace astl

#endif  // ASTL_CONSTANTS_HPP_
