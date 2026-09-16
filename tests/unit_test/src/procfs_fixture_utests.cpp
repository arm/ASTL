// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../../fuzz/fixtures/minimal_procfs_fixture.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl_utils.hpp"
#include "collector/collector_builder.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "metric/metric_builder.hpp"
#include "topology/procfs_target.hpp"
#include "topology/procfs_topology_plugin.hpp"

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kUpdatedLoadHundredths = 150;
constexpr uint64_t kUpdatedLastPid        = 99;
constexpr double   kExpectedInitialLoad   = 0.25;

auto ReadFile(const fs::path& path) -> std::string {
  std::ifstream input{path};
  REQUIRE(input.good());
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

struct CapturingSink : astl::IRawSampleSink {
  auto SinkRawSamples(const astl::ITarget* target, std::span<astl::RawSampledData> raw_samples)
      -> astl_status_code override {
    last_target = target;
    samples.assign(raw_samples.begin(), raw_samples.end());
    return ASTL_STATUS_SUCCESS;
  }

  const astl::ITarget*              last_target{nullptr};
  std::vector<astl::RawSampledData> samples{};  // NOLINT(readability-redundant-member-init)
};

}  // namespace

// Catch2 assertion macros inflate the reported cognitive complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("MinimalProcfsFixture provides a resettable read-only four-file procfs tree", "[procfs_fixture]") {
  const fs::path                   root = fs::temp_directory_path() / "astl_minimal_procfs_fixture";
  TempFileGuard                    stale_fixture_guard(root);
  astl::fuzz::MinimalProcfsFixture fixture{root};

  std::vector<std::string> filenames;
  for (const auto& entry : fs::directory_iterator{root}) {
    filenames.push_back(entry.path().filename().string());
  }
  std::ranges::sort(filenames);
  REQUIRE(filenames == std::vector<std::string>{"loadavg", "meminfo", "stat", "uptime"});
  REQUIRE(ReadFile(root / "loadavg") == "0.25 0.50 0.75 1/8 42\n");
  REQUIRE(ReadFile(root / "uptime") == "1000.00 800.00\n");

  constexpr auto write_permissions = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
  CHECK((fs::status(root).permissions() & write_permissions) == fs::perms::none);
  for (const auto& filename : filenames) {
    CHECK((fs::status(root / filename).permissions() & write_permissions) == fs::perms::none);
  }

  astl::fuzz::ProcfsFixtureSnapshot next;
  next.load_1m_hundredths = kUpdatedLoadHundredths;
  next.last_pid           = kUpdatedLastPid;
  fixture.Reset(next);

  REQUIRE(ReadFile(root / "loadavg") == "1.50 0.50 0.75 1/8 99\n");
  CHECK((fs::status(root).permissions() & write_permissions) == fs::perms::none);
}

// Catch2 assertion macros inflate the reported cognitive complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Configured procfs root is retained by discovered targets", "[procfs_fixture][TopologyManager]") {
  const fs::path                   root = fs::temp_directory_path() / "astl_configured_procfs_fixture";
  TempFileGuard                    stale_fixture_guard(root);
  astl::fuzz::MinimalProcfsFixture fixture{root};
  EnvVarGuard                      procfs_root_guard(astl::EnvVar::ASTL_PROCFS_ROOT, root.string());

  auto configuration = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration.has_value());
  REQUIRE(configuration->procfs_root_path == root);

  auto targets = astl::ProcfsTopologyPlugin::ScanForTargets(*configuration);
  REQUIRE(targets.has_value());
  REQUIRE(targets->size() == 1);
  const auto* target = dynamic_cast<const astl::ProcfsTarget*>(targets->front().get());
  REQUIRE(target != nullptr);
  REQUIRE(target->ProcfsRootPath() == root);

  auto metric_manager = astl::BuildMetricManager(*targets, *configuration, std::nullopt);
  REQUIRE(metric_manager.has_value());
  auto counters = (*metric_manager)->GetAvailableCounters(target);
  REQUIRE(counters.has_value());

  astl_counter_handle_t load_counter = nullptr;
  for (const auto* const counter : *counters) {
    astl_counter_props_t properties{};
    REQUIRE((*metric_manager)->GetCounterProperties(counter, &properties) == ASTL_STATUS_SUCCESS);
    if (std::string_view{properties.name} == "loadavg.1m") {
      load_counter = counter;
      break;
    }
  }
  REQUIRE(load_counter != nullptr);

  auto operations = (*metric_manager)->GetCounterRequiredOperations({&load_counter, 1}, target);
  REQUIRE(operations.has_value());
  auto collector_manager = astl::BuildCollectorManager(*targets, *configuration);
  REQUIRE(collector_manager.has_value());
  CapturingSink sink;
  REQUIRE((*collector_manager)->RegisterRawSampleSink(&sink) == ASTL_STATUS_SUCCESS);

  astl_collection_params_t params{};
  params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;
  REQUIRE((*collector_manager)->ConfigureCollectionOnTarget(target, params, std::move(*operations)) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE((*collector_manager)->ReadImmediateOnTarget(target) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.last_target == target);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(kExpectedInitialLoad));
}
