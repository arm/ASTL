// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_TARGET_CONFIGURATION_HPP_
#define SCMI_TARGET_CONFIGURATION_HPP_

#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "config/astl_configuration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"

namespace astl {

struct ITarget;

/** Information shared by targets that use the same SCMI specification and metric declaration. */
struct ScmiUuidSpecificationInfo {
  scmi::spec::Uuid            uuid;
  std::filesystem::path       specification_file;
  std::filesystem::path       metric_declaration_file;
  std::vector<const ITarget*> applicable_targets;
};

[[nodiscard]] auto GetScmiTelemetrySubdirectory(const ITarget& target) -> std::string_view;

[[nodiscard]] auto ApplyConfiguredScmiTargetNames(const AstlConfiguration&                     configuration,
                                                  const std::vector<std::unique_ptr<ITarget>>& targets)
    -> astl_status_code;

[[nodiscard]] auto LookUpScmiSpecificationFiles(const AstlConfiguration&           configuration,
                                                const std::vector<const ITarget*>& scmi_targets)
    -> std::expected<std::vector<ScmiUuidSpecificationInfo>, astl_status_code>;

}  // namespace astl

#endif  // SCMI_TARGET_CONFIGURATION_HPP_
