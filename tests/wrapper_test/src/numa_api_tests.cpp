// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <fstream>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "orchestrator/orchestrator_builder.hpp"

#if defined(ASTL_INCLUDE_NUMA)
namespace {
struct NumaSessionGuard {
  NumaSessionGuard()                                           = default;
  NumaSessionGuard(const NumaSessionGuard&)                    = delete;
  auto operator=(const NumaSessionGuard&) -> NumaSessionGuard& = delete;
  NumaSessionGuard(NumaSessionGuard&&)                         = delete;
  auto operator=(NumaSessionGuard&&) -> NumaSessionGuard&      = delete;
  ~NumaSessionGuard() { astl::Orchestrator::ResetInstance(); }
};
}  // namespace
TEST_CASE("NUMA public metric discovery and percentage collection", "[wrapper][numa]") {
  const auto root = std::filesystem::temp_directory_path() /
                    ("astl-numa-api-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempFileGuard cleanup{root};
  std::filesystem::create_directories(root / "node3");
  auto write = [&](uint64_t local, uint64_t remote) {
    std::ofstream file{root / "node3" / "numastat"};
    file << "numa_hit " << local << "\nnuma_miss " << remote << "\ninterleave_hit 0\nlocal_node " << local
         << "\nother_node " << remote << "\n";
  };
  write(100, 100);
  EnvVarGuard      selected{astl::EnvVar::ASTL_COLLECTORS, "numa"};
  EnvVarGuard      path{astl::EnvVar::ASTL_NUMA_ROOT, root.string()};
  NumaSessionGuard session;
  auto             config = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(config);
  REQUIRE(BuildOrchestrator(*config) == ASTL_STATUS_SUCCESS);
  uint32_t            target_count = 1;
  astl_target_props_t target{};
  target.size = sizeof(target);
  ASTL_INIT_STRUCT(astl_get_targets_params_t, targets, .flags = 0, .targets = &target, .target_count = &target_count);
  REQUIRE(astlGetTargets(&targets) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);
  CHECK(std::string{target.name} == "numa");
  uint32_t count = 0;
  ASTL_INIT_STRUCT(astl_get_metric_count_params_t, metric_count, .flags = 0, .target_handle = target.handle,
                   .metric_count = &count);
  REQUIRE(astlGetMetricCountOnTarget(&metric_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(count == 8);
  std::vector<astl_metric_props_t> props(count);
  props[0].size = sizeof(astl_metric_props_t);
  ASTL_INIT_STRUCT(astl_get_metrics_params_t, metrics, .flags = 0, .target_handle = target.handle,
                   .metrics = props.data(), .metric_count = &count);
  REQUIRE(astlGetMetricsOnTarget(&metrics) == ASTL_STATUS_SUCCESS);
  astl_metric_handle_t handle{};
  for (const auto& prop : props) {
    if (std::string{prop.name} == "Linux_numa_local_percent") {
      handle = prop.handle;
    }
  }
  REQUIRE(handle != nullptr);
  ASTL_INIT_STRUCT(astl_collection_params_t, collection, .flags = 0, .sampling_interval = 1,
                   .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE);
  ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, configure, .flags = 0,
                   .target_handle = target.handle, .collection_params = &collection, .metric_handles = &handle,
                   .metric_count = 1);
  REQUIRE(astlConfigureMetricCollectionOnTarget(&configure) == ASTL_STATUS_SUCCESS);
  ASTL_INIT_STRUCT(astl_start_collection_on_target_params_t, start, .flags = 0, .target_handle = target.handle);
  REQUIRE(astlStartCollectionOnTarget(&start) == ASTL_STATUS_SUCCESS);
  ASTL_INIT_STRUCT(astl_read_immediate_on_target_params_t, read, .flags = 0, .target_handle = target.handle);
  REQUIRE(astlReadImmediateOnTarget(&read) == ASTL_STATUS_SUCCESS);
  write(175, 125);
  REQUIRE(astlReadImmediateOnTarget(&read) == ASTL_STATUS_SUCCESS);
  ASTL_INIT_STRUCT(astl_stop_collection_on_target_params_t, stop, .flags = 0, .target_handle = target.handle);
  REQUIRE(astlStopCollectionOnTarget(&stop) == ASTL_STATUS_SUCCESS);
  astl_sample_t sample{};
  uint32_t      sample_count = 1;
  ASTL_INIT_STRUCT(astl_get_metric_samples_on_target_params_t, samples, .flags = 0, .target_handle = target.handle,
                   .metric_handle = handle, .samples = &sample, .sample_count = &sample_count, .start_ts = 0,
                   .end_ts = 0);
  REQUIRE(astlGetMetricSamplesOnTarget(&samples) == ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count == 1);
  CHECK(sample.value.fp64 == Catch::Approx(75.0));
}
#endif
