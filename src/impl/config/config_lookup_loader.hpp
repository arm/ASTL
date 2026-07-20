// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef CONFIG_LOOKUP_LOADER_HPP_
#define CONFIG_LOOKUP_LOADER_HPP_

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"
#include "config/json_file_utils.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"

namespace astl::config {

namespace detail {

inline auto ResolvePathRelativeTo(const std::filesystem::path& base_dir, const std::filesystem::path& path)
    -> std::filesystem::path {
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return (base_dir / path).lexically_normal();
}

inline auto FindLookupFiles(const std::filesystem::path& root_dir, std::string_view file_name)
    -> std::expected<std::vector<std::filesystem::path>, astl_status_code> {
  std::vector<std::filesystem::path> lookup_files;

  try {
    if (!std::filesystem::is_directory(root_dir)) {
      ASTL_LOG_ERROR("Lookup root directory does not exist: {}", root_dir.string());
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      if (entry.path().filename() == file_name) {
        lookup_files.push_back(entry.path());
      }
    }
  } catch (const std::exception& e) {
    ASTL_LOG_ERROR("Failed to scan lookup files under {}: {}", root_dir.string(), e.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  std::ranges::sort(lookup_files);
  return lookup_files;
}

}  // namespace detail

inline auto LoadPlatformLookupFragments(const std::filesystem::path& metrics_dir)
    -> std::expected<metrics::spec::PlatformLookup, astl_status_code> {
  auto lookup_files = detail::FindLookupFiles(metrics_dir, "platform_lookup.json");
  if (!lookup_files.has_value()) {
    return std::unexpected(lookup_files.error());
  }

  metrics::spec::PlatformLookup merged_lookup;
  for (const auto& lookup_file : *lookup_files) {
    auto lookup = TryParseJsonFile<metrics::spec::PlatformLookup>(lookup_file);
    if (!lookup.has_value()) {
      return std::unexpected(lookup.error());
    }

    merged_lookup.last_updated = lookup->last_updated;
    for (auto& entry : lookup->metric_files_by_platform_uuid) {
      entry.metrics_declaration_file.resolved_metrics_file =
          detail::ResolvePathRelativeTo(lookup_file.parent_path(), entry.metrics_declaration_file.metrics_file);
      merged_lookup.metric_files_by_platform_uuid.emplace_back(std::move(entry));
    }
  }

  return merged_lookup;
}

inline auto LoadRepoMetaFragments(const std::filesystem::path& scmi_specification_dir)
    -> std::expected<scmi::spec::RepoMeta, astl_status_code> {
  auto repo_meta_files = detail::FindLookupFiles(scmi_specification_dir, "repometa.json");
  if (!repo_meta_files.has_value()) {
    return std::unexpected(repo_meta_files.error());
  }

  scmi::spec::RepoMeta merged_repo_meta;
  for (const auto& repo_meta_file : *repo_meta_files) {
    auto repo_meta = TryParseJsonFile<scmi::spec::RepoMeta>(repo_meta_file);
    if (!repo_meta.has_value()) {
      return std::unexpected(repo_meta.error());
    }

    merged_repo_meta.last_updated = repo_meta->last_updated;
    for (auto& entry : repo_meta->spec_files_by_uuid) {
      entry.spec_file.resolved_specification_file =
          detail::ResolvePathRelativeTo(repo_meta_file.parent_path(), entry.spec_file.specification_file);
      merged_repo_meta.spec_files_by_uuid.emplace_back(std::move(entry));
    }
  }

  return merged_repo_meta;
}

}  // namespace astl::config

#endif  // CONFIG_LOOKUP_LOADER_HPP_
