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
  static constexpr std::string_view kNamePrefix{"scmi_"};

  ScmiTarget(std::string name, std::string description, std::string telemetry_subdirectory, Target* parent = nullptr,
             std::optional<std::string> uuid = std::nullopt)
      : astl::Target(std::move(name), std::move(description), CollectorType::SCMI, parent, std::move(uuid),
                     telemetry_subdirectory),
        _telemetry_subdirectory(std::move(telemetry_subdirectory)) {}

  ~ScmiTarget() override                   = default;
  ScmiTarget(const ScmiTarget&)            = default;
  ScmiTarget& operator=(const ScmiTarget&) = default;
  ScmiTarget(ScmiTarget&&)                 = default;
  ScmiTarget& operator=(ScmiTarget&&)      = default;

  auto TelemetrySubdirectory() const -> std::string const& { return _telemetry_subdirectory; }

  static auto NameForTelemetrySubdirectory(std::string_view telemetry_subdirectory) -> std::string {
    return std::string{kNamePrefix} + std::string{telemetry_subdirectory};
  }

  static auto TryInferTelemetrySubdirectoryFromName(std::string_view target_name) -> std::optional<std::string_view> {
    if (target_name.starts_with(kNamePrefix)) {
      return target_name.substr(kNamePrefix.size());
    }
    if (!target_name.empty()) {
      return target_name;
    }
    return std::nullopt;
  }

  static auto TelemetrySubdirectoryForTarget(const ITarget& target) -> std::string_view {
    if (const auto* scmi_target = dynamic_cast<const ScmiTarget*>(&target)) {
      return scmi_target->TelemetrySubdirectory();
    }
    if (const auto collector_target_path = target.CollectorTargetPath();
        collector_target_path.has_value() && !collector_target_path->empty()) {
      return *collector_target_path;
    }
    return TryInferTelemetrySubdirectoryFromName(target.Name()).value_or(target.Name());
  }

 private:
  std::string _telemetry_subdirectory;
};

}  // namespace astl

#endif  // SCMI_TARGET_HPP_
