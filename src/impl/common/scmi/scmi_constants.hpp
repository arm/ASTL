#ifndef ASTL_CONSTANTS_HPP_
#define ASTL_CONSTANTS_HPP_

#include <chrono>
#include <string_view>

namespace astl {

constexpr std::string_view kDefaultScmiSysfsTelemetryRootPath = "/sys/fs/arm_telemetry";

// the rate in KHz that a timestamp increments for a given data event, used to interpret timestamp values
using kilohertz = uint32_t;

}  // namespace astl

#endif  // ASTL_CONSTANTS_HPP_
