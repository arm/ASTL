// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <memory>

#include "astl/astl_errors.h"
#include "collector/collector_builder.hpp"
#include "common/system_info.hpp"
#include "config/astl_configuration.hpp"
#include "metric/metric_builder.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_builder.hpp"
#include "topology/topology_builder.hpp"

namespace fs = std::filesystem;

namespace {
auto MakeUniqueCacheDirPath() -> fs::path {
  static std::atomic<uint64_t> unique_suffix{0};
  const auto now_nanos = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto suffix    = unique_suffix.fetch_add(1, std::memory_order_relaxed);
  return fs::temp_directory_path() / ("astl-" + std::to_string(now_nanos) + "-" + std::to_string(suffix));
}
}  // namespace

/** @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc.
 */
auto BuildOrchestrator(const astl::AstlConfiguration& configuration) -> astl_status_code {
  fs::path cache_dir_path = MakeUniqueCacheDirPath();
  if (configuration.load_file_path) {
    auto status = astl::Orchestrator::LoadFromFile(*configuration.load_file_path, cache_dir_path);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Orchestrator::GetInstance failed to load state from ASTL file '{}'",
                     configuration.load_file_path->string());
      return status;
    }
  } else {
    astl::ClearLoadedPlatformInfo();
  }

  auto topology_manager = astl::BuildTopologyManager(configuration, cache_dir_path);
  if (!topology_manager) {
    return topology_manager.error();
  }

  // TODO(ASTL-279) - Once state machine is in place, we can skip building collector manager if loading from file
  auto collector_manager = astl::BuildCollectorManager(topology_manager.value()->GetTargets(), configuration);
  if (!collector_manager) {
    return collector_manager.error();
  }

  auto metric_manager = astl::BuildMetricManager(topology_manager.value()->GetTargets(), configuration, cache_dir_path);
  if (!metric_manager) {
    return metric_manager.error();
  }

  auto output_manager = astl::BuildOutputManager();
  if (!output_manager) {
    return output_manager.error();
  }

  // wire it all up in our new Orchestrator and replace the global instance with it.
  // Note, Orchestrator destructor should shut down all collection, etc.
  try {
    astl::Orchestrator::InitializeInstance(std::move(topology_manager.value()), std::move(collector_manager.value()),
                                           std::move(metric_manager.value()), std::move(output_manager.value()),
                                           cache_dir_path);
  } catch (const std::invalid_argument& e) {
    ASTL_LOG_ERROR("Orchestrator::InitializeInstance threw an exception: {}", e.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  // the orchestrator owns targets
  return ASTL_STATUS_SUCCESS;
}
