// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <filesystem>
#include <memory>

#include "astl/astl_errors.h"
#include "libsensors/libsensors_topology_plugin.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "target.hpp"
#include "topology/i_topology_manager.hpp"
#include "topology/scmi_topology_plugin.hpp"
#include "topology/topology_manager.hpp"

namespace astl {

// Helper function which runs a single topology plugin and does some error handling
// 2nd parameter is a function pointer to the ScanForTargets() member of a given plugin
auto ActivatePlugin(std::vector<std::unique_ptr<ITarget>>& targets, const AstlConfiguration& configuration,
                    std::expected<std::vector<std::unique_ptr<ITarget>>, astl_status_code> (*scanFunc)(
                        const AstlConfiguration&)) -> void {
  auto targets_detected_from_this_plugin = scanFunc(configuration);
  if (!targets_detected_from_this_plugin) {
    throw targets_detected_from_this_plugin.error();
  }

  auto validated_new_targets = std::move(targets_detected_from_this_plugin.value());

  // std::vector::append_range() would be better,
  // but g++ 13.1.0 doesn't seem to implement __cpp_lib_containers_ even when using -std=c++23
  targets.reserve(targets.size() + validated_new_targets.size());
  for (size_t ix = 0; ix < validated_new_targets.size(); ix++) {
    targets.push_back(std::move(validated_new_targets[ix]));
  }
}

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

  auto topology_manager = ProtobufSerDes::Deserialize<std::unique_ptr<ITopologyManager>>(topology_file);
  if (!topology_manager.has_value()) {
    return std::unexpected(topology_manager.error());
  }
  return topology_manager;
}

auto BuildTopologyManager(const AstlConfiguration& configuration, std::optional<std::filesystem::path> cache_dir_path)
    -> std::expected<std::unique_ptr<ITopologyManager>, astl_status_code> {
  std::vector<std::unique_ptr<ITarget>> targets;

  if (configuration.load_file_path.has_value()) {
    return BuildTopologyManagerFromASTLFile(cache_dir_path.value());
  }

  try {
    // Add more topology plugins here by calling ActivatePlugin on each
    ActivatePlugin(targets, configuration, ScmiTopologyPlugin::ScanForTargets);
    ActivatePlugin(targets, configuration, LibsensorsTopologyPlugin::ScanForTargets);
  } catch (astl_status_code& error_code) {
    return std::unexpected(error_code);
  }

  return std::make_unique<TopologyManager>(std::move(targets));
}

}  // namespace astl
