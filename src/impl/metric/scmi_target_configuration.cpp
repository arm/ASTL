// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/scmi_target_configuration.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "astl_logger.hpp"
#include "config/config_lookup_loader.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "target.hpp"

namespace astl {

static auto ReplaceAll(std::string value, std::string_view needle, std::string_view replacement) -> std::string {
  std::size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    value.replace(position, needle.size(), replacement);
    position += replacement.size();
  }
  return value;
}

auto GetScmiTelemetrySubdirectory(const ITarget& target) -> std::string_view {
  if (const auto collector_target_path = target.CollectorTargetPath();
      collector_target_path.has_value() && !collector_target_path->empty()) {
    return *collector_target_path;
  }
  ASTL_LOG_WARNING(
      "SCMI target '{}' is missing collector path metadata; '{{telemetry_subdirectory}}' resolves to "
      "an empty string",
      target.Name());
  return {};
}

static auto ResolveScmiTargetNameTemplate(std::string_view name_template, const ITarget& target) -> std::string {
  auto resolved_name = std::string{name_template};
  resolved_name      = ReplaceAll(std::move(resolved_name), "{target_name}", target.Name());
  resolved_name =
      ReplaceAll(std::move(resolved_name), "{telemetry_subdirectory}", GetScmiTelemetrySubdirectory(target));
  return resolved_name;
}

/** @brief best-effort helper to get the normalized SCMI UUID from a target, returning nullopt if any step fails. */
static auto GetUuidFromTarget(const ITarget* target) -> std::optional<scmi::spec::Uuid> {
  const auto* concrete_target = dynamic_cast<const Target*>(target);
  if (!concrete_target) {
    ASTL_LOG_WARNING("Target cannot be cast to concrete Target type");
    return std::nullopt;
  }
  auto uuid_opt = concrete_target->GetUuid();
  if (!uuid_opt.has_value() || uuid_opt->empty()) {
    ASTL_LOG_WARNING("Target {} has no UUID", target->Name());
    return std::nullopt;
  }
  const auto uuid_res = scmi::spec::GetNormalizedUuid(uuid_opt.value());
  if (!uuid_res) {
    ASTL_LOG_WARNING("Target {} has invalid SCMI UUID {}: error code {}", target->Name(), uuid_opt.value(),
                     astlStatusString(uuid_res.error()));
    return std::nullopt;
  }
  return *uuid_res;
}

static auto ApplyConfiguredNameForScmiTarget(Target&                              concrete_target,
                                             const metrics::spec::PlatformLookup& platform_lookup) -> astl_status_code {
  auto uuid = GetUuidFromTarget(&concrete_target);
  if (!uuid.has_value()) {
    return ASTL_STATUS_SUCCESS;
  }

  auto metric_file_element = FindMetricsFileElementByUuid(platform_lookup, *uuid);
  if (!metric_file_element.has_value() || !metric_file_element->name.has_value()) {
    return ASTL_STATUS_SUCCESS;
  }

  const auto configured_name = ResolveScmiTargetNameTemplate(*metric_file_element->name, concrete_target);
  if (configured_name.empty()) {
    ASTL_LOG_ERROR("Configured SCMI target name resolved to an empty string for UUID {}", uuid->normalized_value);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  concrete_target.SetName(configured_name);
  return ASTL_STATUS_SUCCESS;
}

auto ApplyConfiguredScmiTargetNames(const AstlConfiguration&                     configuration,
                                    const std::vector<std::unique_ptr<ITarget>>& targets) -> astl_status_code {
  const auto has_scmi_targets = std::ranges::any_of(
      targets, [](const auto& target_ptr) { return target_ptr->GetCollectorType() == CollectorType::SCMI; });
  if (!has_scmi_targets) {
    return ASTL_STATUS_SUCCESS;
  }

  auto platform_lookup = config::LoadPlatformLookupFragments(configuration.metrics_dir_path);
  if (!platform_lookup.has_value()) {
    return platform_lookup.error();
  }

  astl_status_code status = ASTL_STATUS_SUCCESS;
  for (const auto& target_ptr : targets) {
    if (target_ptr->GetCollectorType() != CollectorType::SCMI) {
      continue;
    }

    auto* concrete_target = dynamic_cast<Target*>(target_ptr.get());
    if (!concrete_target) {
      ASTL_LOG_WARNING("SCMI target {} cannot be cast to concrete Target type", target_ptr->Name());
      continue;
    }
    status = ApplyConfiguredNameForScmiTarget(*concrete_target, *platform_lookup);
    if (status != ASTL_STATUS_SUCCESS) {
      break;
    }
  }

  return status;
}

struct ScmiSpecificationLookupContext {
  const scmi::spec::RepoMeta*          repo_meta;
  const metrics::spec::PlatformLookup* platform_lookup;
};

static auto AddScmiSpecificationInfoForTarget(std::vector<ScmiUuidSpecificationInfo>& platform_specifications,
                                              const ITarget*                          target,
                                              const ScmiSpecificationLookupContext&   lookup_context) -> void {
  auto uuid_result = GetUuidFromTarget(target);
  if (!uuid_result.has_value()) {
    return;
  }
  const auto uuid = *uuid_result;

  auto spec_file = FindSpecFileByUuid(*lookup_context.repo_meta, uuid);
  if (!spec_file) {
    ASTL_LOG_WARNING("No SCMI specification found for UUID {}", uuid.normalized_value);
    return;
  }

  auto metric_file_element = FindMetricsFileElementByUuid(*lookup_context.platform_lookup, uuid);
  if (!metric_file_element) {
    ASTL_LOG_WARNING("No metrics declaration found for UUID {}", uuid.normalized_value);
    return;
  }

  auto resolved_specification_file      = spec_file->resolved_specification_file.empty()
                                              ? std::filesystem::path{spec_file->specification_file}
                                              : spec_file->resolved_specification_file;
  auto resolved_metric_declaration_file = metric_file_element->resolved_metrics_file.empty()
                                              ? std::filesystem::path{metric_file_element->metrics_file}
                                              : metric_file_element->resolved_metrics_file;

  // Group by the resolved specification/metric-declaration file pair rather than by the target's full UUID.
  auto existing_entry = std::find_if(platform_specifications.begin(), platform_specifications.end(),
                                     [&](const ScmiUuidSpecificationInfo& info) {
                                       return info.specification_file == resolved_specification_file &&
                                              info.metric_declaration_file == resolved_metric_declaration_file;
                                     });
  if (existing_entry != platform_specifications.end()) {
    existing_entry->applicable_targets.push_back(target);
    return;
  }

  platform_specifications.emplace_back(
      ScmiUuidSpecificationInfo{.uuid                    = uuid,
                                .specification_file      = std::move(resolved_specification_file),
                                .metric_declaration_file = std::move(resolved_metric_declaration_file),
                                .applicable_targets      = {target}});
}

auto LookUpScmiSpecificationFiles(const AstlConfiguration&           configuration,
                                  const std::vector<const ITarget*>& scmi_targets)
    -> std::expected<std::vector<ScmiUuidSpecificationInfo>, astl_status_code> {
  auto repo_meta = config::LoadRepoMetaFragments(configuration.scmi_specification_dir);
  if (!repo_meta.has_value()) {
    return std::unexpected(repo_meta.error());
  }

  auto platform_lookup = config::LoadPlatformLookupFragments(configuration.metrics_dir_path);
  if (!platform_lookup.has_value()) {
    return std::unexpected(platform_lookup.error());
  }

  std::vector<ScmiUuidSpecificationInfo> platform_specifications;
  platform_specifications.reserve(scmi_targets.size());
  const ScmiSpecificationLookupContext lookup_context{
      .repo_meta       = &(*repo_meta),
      .platform_lookup = &(*platform_lookup),
  };

  for (const auto* target : scmi_targets) {
    AddScmiSpecificationInfoForTarget(platform_specifications, target, lookup_context);
  }

  return platform_specifications;
}

}  // namespace astl
