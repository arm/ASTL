// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "topology/topology_builder.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>

#include "astl/astl_errors.h"
#include "serdes/protobuf_serdes.hpp"
#include "target.hpp"
#include "topology/topology_manager.hpp"

namespace astl {

auto BuildTopologyManagerFromASTLFile(const std::filesystem::path& cache_dir_path)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code> {
  const std::filesystem::path topology_manager_file_path = cache_dir_path / kTopologyManagerFileName;

  if (!std::filesystem::is_directory(cache_dir_path)) {
    ASTL_LOG_DEBUG("Creating ASTL cache directory: {}", cache_dir_path.string());
    std::filesystem::create_directories(cache_dir_path);
  }

  std::ifstream topology_file(topology_manager_file_path, std::ios::binary | std::ios::in);
  if (!topology_file) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  return ProtobufSerDes::Deserialize<std::unique_ptr<ITopologyManager>>(topology_file);
}

auto BuildTopologyManager(const AstlConfiguration& configuration, std::optional<std::filesystem::path> cache_dir_path,
                          CollectorBackends backends)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code> {
  std::vector<std::unique_ptr<ITarget>> targets;

  if (configuration.load_file_path.has_value()) {
    if (!cache_dir_path.has_value()) {
      ASTL_LOG_ERROR(
          "configuration.load_file_path has value: `{}` but cache_dir_path is not set."
          " Cannot load topology from ASTL file without a cache directory path.",
          configuration.load_file_path->string());
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return BuildTopologyManagerFromASTLFile(cache_dir_path.value());
  }

  if (auto status = ValidateCollectorBackends(backends); status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(status);
  }
  std::unordered_set<std::string> seen_names;
  for (const auto& backend : backends) {
    if (backend.discover == nullptr || !configuration.collectors.IsEnabled(backend.type)) {
      continue;
    }
    auto discovered = backend.discover(configuration);
    if (!discovered) {
      return std::unexpected(discovered.error());
    }
    for (auto& target : *discovered) {
      if (!target) {
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      if (target->GetCollectorType() != backend.type) {
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      if (seen_names.insert(target->Name()).second) {
        targets.push_back(std::move(target));
      } else {
        ASTL_LOG_WARNING("Duplicate target name '{}'; discarding the duplicate target and continuing", target->Name());
      }
    }
  }

  return std::make_unique<TopologyManager>(std::move(targets));
}

}  // namespace astl
