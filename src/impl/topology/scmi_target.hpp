// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_TARGET_HPP_
#define SCMI_TARGET_HPP_

#include <optional>
#include <string>

#include "target.hpp"

namespace astl {

/**
 * @brief SCMI-specific target metadata, including the sysfs telemetry subdirectory for this target.
 */
class ScmiTarget : public astl::Target {
 public:
  ScmiTarget(std::string name, std::string description, std::string telemetry_subdirectory, Target* parent = nullptr,
             std::optional<std::string> uuid = std::nullopt)
      : astl::Target(std::move(name), std::move(description), CollectorType::SCMI, parent, std::move(uuid)),
        _telemetry_subdirectory(std::move(telemetry_subdirectory)) {}

  ~ScmiTarget() override                   = default;
  ScmiTarget(const ScmiTarget&)            = default;
  ScmiTarget& operator=(const ScmiTarget&) = default;
  ScmiTarget(ScmiTarget&&)                 = default;
  ScmiTarget& operator=(ScmiTarget&&)      = default;

  auto TelemetrySubdirectory() const -> std::string const& { return _telemetry_subdirectory; }

 private:
  std::string _telemetry_subdirectory;
};

}  // namespace astl

#endif  // SCMI_TARGET_HPP_
