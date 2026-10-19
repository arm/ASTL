// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <limits>
#include <sstream>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "collector/collector_builder.hpp"
#include "metric/metric_builder.hpp"
#include "metric/metric_manager.hpp"
#include "topology/topology_builder.hpp"
#include "topology/topology_manager.hpp"
#if defined(ASTL_INCLUDE_WINDOWS)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <pdh.h>
#  include <windows.h>
#endif

#include "windows/windows_metric_builder.hpp"
#include "windows/windows_system_collector.hpp"

namespace {
using astl::CollectionConfiguration;
using astl::CollectionOperations;
using astl::CollectorCapability;
using astl::CollectorType;
using astl::IRawSampleSink;
using astl::ITarget;
using astl::IWindowsSystemApi;
using astl::OperationSequence;
using astl::RawSampledData;
using astl::SortWindowsProcessorInstances;
using astl::WindowsMemorySnapshot;
using astl::WindowsReadOperation;
using astl::WindowsSystemCollector;
using astl::WindowsSystemField;
using astl::WindowsSystemTarget;
class FakeWindowsApi : public IWindowsSystemApi {
 public:
  WindowsMemorySnapshot    memory{1000, 250};
  std::vector<std::string> instances;
  unsigned                 cpu_reads{}, memory_reads{}, resets{};
  bool                     primed{false};
  astl_status_code         read_status{ASTL_STATUS_SUCCESS};
  std::optional<double>    utilization{37.5};
  auto                     DiscoverProcessors() -> std::expected<std::vector<std::string>, astl_status_code> override {
    return std::vector<std::string>{"0,0", "1,0"};
  }
  auto ConfigureCpu(const std::vector<std::string>& selected) -> astl_status_code override {
    instances = selected;
    return ResetCpu();
  }
  auto ResetCpu() -> astl_status_code override {
    ++resets;
    primed = false;
    return ASTL_STATUS_SUCCESS;
  }
  auto ReadCpu() -> std::expected<std::unordered_map<std::string, std::optional<double>>, astl_status_code> override {
    ++cpu_reads;
    if (read_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(read_status);
    }
    std::unordered_map<std::string, std::optional<double>> values;
    for (const auto& instance : instances) {
      values[instance] = primed ? utilization : std::nullopt;
    }
    primed = true;
    return values;
  }
  auto ReadMemory() -> std::expected<WindowsMemorySnapshot, astl_status_code> override {
    ++memory_reads;
    if (read_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(read_status);
    }
    return memory;
  }
};
class Sink : public IRawSampleSink {
 public:
  std::vector<RawSampledData> samples;
  auto SinkRawSamples(const ITarget* /*target*/, std::span<RawSampledData> values) -> astl_status_code override {
    samples.insert(samples.end(), values.begin(), values.end());
    return ASTL_STATUS_SUCCESS;
  }
};
struct Fixture {
  WindowsSystemTarget target{
      {"0,0", "1,0"}
  };
  FakeWindowsApi*        api{};
  Sink                   sink;
  WindowsSystemCollector collector{MakeApi()};
  auto                   MakeApi() -> std::unique_ptr<IWindowsSystemApi> {
    auto result = std::make_unique<FakeWindowsApi>();
    api         = result.get();
    return result;
  }
  Fixture() { collector.SetRawSampleSink(&sink); }
  void Configure(std::vector<WindowsSystemField> fields, astl_collection_mode_t mode = ASTL_COLLECTION_MODE_IMMEDIATE) {
    OperationSequence reads;
    for (const auto field : fields) {
      reads.push_back(std::make_unique<WindowsReadOperation>(field, "0,0"));
    }
    CollectionOperations     operations{.operationsBeforeStart{},
                                        .operationsAtStart{},
                                        .operationsOnSample{std::move(reads)},
                                        .operationsAtStop{},
                                        .samplingInterval{},
                                        .requirements{CollectorCapability{CollectorType::WINDOWS}}};
    astl_collection_params_t params{};
    params.collection_mode   = mode;
    params.sampling_interval = 1;
    REQUIRE(collector.ConfigureCollection(CollectionConfiguration{&target, std::move(operations), params}) ==
            ASTL_STATUS_SUCCESS);
  }
};
}  // namespace

TEST_CASE("Windows processor indices sort numeric native identities and omit totals", "[windows_collector]") {
  CHECK(SortWindowsProcessorInstances({"1,0", "0,10", "0,2", "_Total", "0,_Total", "x,0", "0,2", "0,", "2,1x"}) ==
        std::vector<std::string>{"0,2", "0,10", "1,0"});
  std::vector<std::string> instances;
  for (unsigned group = 0; group < 3; ++group) {
    for (unsigned cpu = 0; cpu < 64; ++cpu) {
      instances.push_back(std::to_string(group) + "," + std::to_string(cpu));
    }
  }
  const auto sorted = SortWindowsProcessorInstances(instances);
  REQUIRE(sorted.size() == 192);
  CHECK(sorted[64] == "1,0");
  CHECK(sorted.back() == "2,63");
}

TEST_CASE("Windows memory metrics share a snapshot and retain exact byte counts", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure(
      {WindowsSystemField::MEMORY_TOTAL, WindowsSystemField::MEMORY_USED, WindowsSystemField::MEMORY_UTILIZATION});
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 3);
  CHECK(fixture.api->memory_reads == 1);
  CHECK(fixture.api->cpu_reads == 0);
  CHECK(fixture.sink.samples[0].get<uint64_t>() == 1000);
  CHECK(fixture.sink.samples[1].get<uint64_t>() == 750);
  CHECK(fixture.sink.samples[2].get<double>() == Catch::Approx(75.0));
}

TEST_CASE("Windows CPU warm-up does not suppress memory and reads all CPUs once", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure(
      {WindowsSystemField::CPU_UTILIZATION, WindowsSystemField::CPU_UTILIZATION, WindowsSystemField::MEMORY_TOTAL});
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 1);
  REQUIRE(fixture.api->instances.size() == 1);
  CHECK(fixture.api->cpu_reads == 1);
  fixture.sink.samples.clear();
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 3);
  CHECK(fixture.sink.samples[0].get<double>() == Catch::Approx(37.5));
  CHECK(fixture.api->cpu_reads == 2);
}

TEST_CASE("Windows collector resets CPU baselines on start and resume", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure({WindowsSystemField::CPU_UTILIZATION});
  REQUIRE(fixture.collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.empty());
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 1);
  REQUIRE(fixture.collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  fixture.sink.samples.clear();
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.empty());
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.size() == 1);
  REQUIRE(fixture.collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.collector.ClearCollectionState() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("Windows collector rejects failed memory reads and invalid memory accounting", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure({WindowsSystemField::MEMORY_UTILIZATION});
  SECTION("API failure") { fixture.api->read_status = ASTL_STATUS_INTERNAL_ERROR; }
  SECTION("zero total") { fixture.api->memory.total = 0; }
  SECTION("available exceeds total") { fixture.api->memory.available = 1001; }
  CHECK(fixture.collector.ReadImmediate() != ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.empty());
}

TEST_CASE("Windows collector skips invalid CPU values rather than reporting idle", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure({WindowsSystemField::CPU_UTILIZATION});
  REQUIRE(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  SECTION("missing") { fixture.api->utilization = std::nullopt; }
  SECTION("NaN") { fixture.api->utilization = std::numeric_limits<double>::quiet_NaN(); }
  SECTION("negative") { fixture.api->utilization = -1.0; }
  SECTION("out of range") { fixture.api->utilization = 101.0; }
  CHECK(fixture.collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.empty());
}

#if defined(ASTL_INCLUDE_WINDOWS)
TEST_CASE("Windows native API discovers processors and samples physical memory", "[windows_native]") {
  auto       api        = astl::MakeWindowsSystemApi();
  const auto processors = api->DiscoverProcessors();
  REQUIRE(processors.has_value());
  REQUIRE_FALSE(processors->empty());
  CHECK(*processors == SortWindowsProcessorInstances(*processors));
  REQUIRE(api->ConfigureCpu({"_Total", processors->front()}) == ASTL_STATUS_SUCCESS);
  // Independent query of the same counter used by Performance Monitor. Bracket
  // the collector's reads with the reference reads over the same interval.
  struct ReferenceQuery {
    PDH_HQUERY query{};
    ~ReferenceQuery() {
      if (query) {
        PdhCloseQuery(query);
      }
    }
  } reference;
  REQUIRE(PdhOpenQueryW(nullptr, 0, &reference.query) == ERROR_SUCCESS);
  PDH_HCOUNTER reference_counter{};
  REQUIRE(PdhAddEnglishCounterW(reference.query, L"\\Processor Information(_Total)\\% Processor Time", 0,
                                &reference_counter) == ERROR_SUCCESS);
  REQUIRE(PdhCollectQueryData(reference.query) == ERROR_SUCCESS);
  const auto baseline = api->ReadCpu();
  REQUIRE(baseline.has_value());
  REQUIRE(baseline->size() == 2);
  CHECK_FALSE(baseline->at("_Total").has_value());
  const auto memory = api->ReadMemory();
  REQUIRE(memory.has_value());
  CHECK(memory->total > 0);
  CHECK(memory->available <= memory->total);
  MEMORYSTATUSEX native_memory{};
  native_memory.dwLength = sizeof(native_memory);
  REQUIRE(GlobalMemoryStatusEx(&native_memory));
  CHECK(memory->total == native_memory.ullTotalPhys);

  std::this_thread::sleep_for(std::chrono::milliseconds{1100});
  const auto samples = api->ReadCpu();
  REQUIRE(PdhCollectQueryData(reference.query) == ERROR_SUCCESS);
  PDH_FMT_COUNTERVALUE reference_value{};
  REQUIRE(PdhGetFormattedCounterValue(reference_counter, PDH_FMT_DOUBLE, nullptr, &reference_value) == ERROR_SUCCESS);
  REQUIRE(samples.has_value());
  REQUIRE(samples->at("_Total").has_value());
  CHECK(*samples->at("_Total") >= 0.0);
  CHECK(*samples->at("_Total") <= 100.0);
  CHECK(*samples->at("_Total") == Catch::Approx(reference_value.doubleValue).margin(5.0));
}
#endif

TEST_CASE("Windows metric registration exposes portable names and groups", "[windows_collector]") {
  auto configuration = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration.has_value());
  astl::WindowsSystemTarget target{
      {"0,0", "1,0"}
  };
  astl::Capabilities  capabilities{{astl::CollectorCapability{astl::CollectorType::WINDOWS}},
                                   {astl::SystemCapability{}}};
  astl::MetricManager manager{
      capabilities, {{"cpu", "CPU"}, {"memory", "Memory"}}
  };
  REQUIRE(astl::RegisterWindowsMetrics(*configuration,
                                       {
                                           {astl::CollectorType::WINDOWS, {&target}}
  },
                                       &manager) == ASTL_STATUS_SUCCESS);
  const auto metrics = manager.GetAvailableMetrics(&target);
  REQUIRE(metrics.has_value());
  REQUIRE(metrics->size() == 6);
  std::vector<std::string> names;
  for (const auto* metric : *metrics) {
    astl_metric_props_t props{};
    REQUIRE(manager.GetProperties(metric, &props) == ASTL_STATUS_SUCCESS);
    names.emplace_back(props.name);
  }
  std::ranges::sort(names);
  CHECK(names == std::vector<std::string>{"cpu.0.utilization", "cpu.1.utilization", "cpu.total.utilization",
                                          "memory.total", "memory.used", "memory.utilization"});
  const auto groups = manager.GetMetricGroups(&target);
  REQUIRE(groups.has_value());
  CHECK(groups->size() == 2);
}

TEST_CASE("Windows snapshot samples span collection start and stop", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure({WindowsSystemField::CPU_UTILIZATION, WindowsSystemField::MEMORY_TOTAL},
                    ASTL_COLLECTION_MODE_SNAPSHOT);
  REQUIRE(fixture.collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 1);
  REQUIRE(fixture.collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 3);
  CHECK(fixture.sink.samples[1].get<double>() == Catch::Approx(37.5));
  CHECK(fixture.api->cpu_reads == 2);
  CHECK(fixture.api->memory_reads == 2);
}

TEST_CASE("Windows sampling stops before collector destruction and clearing", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure({WindowsSystemField::MEMORY_TOTAL}, ASTL_COLLECTION_MODE_SAMPLING);
  REQUIRE(fixture.collector.StartCollection() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.collector.ClearCollectionState() == ASTL_STATUS_COLLECTION_ALREADY_RUNNING);
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  REQUIRE(fixture.collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.collector.ClearCollectionState() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.api->instances.empty());
}

TEST_CASE("Windows CPU API errors propagate without producing a value", "[windows_collector]") {
  Fixture fixture;
  fixture.Configure({WindowsSystemField::CPU_UTILIZATION});
  fixture.api->read_status = ASTL_STATUS_INTERNAL_ERROR;
  CHECK(fixture.collector.ReadImmediate() == ASTL_STATUS_INTERNAL_ERROR);
  CHECK(fixture.sink.samples.empty());
}

TEST_CASE("Windows collector allowlist follows build availability", "[windows_collector]") {
  EnvVarGuard selected(astl::EnvVar::ASTL_COLLECTORS, "windows");
  const auto  configuration = astl::AstlConfiguration::CreateConfiguration();
#if defined(ASTL_INCLUDE_WINDOWS)
  REQUIRE(configuration.has_value());
  CHECK(configuration->collectors.windows);
  CHECK_FALSE(configuration->collectors.scmi);
  CHECK_FALSE(configuration->collectors.procfs);
  CHECK_FALSE(configuration->collectors.libsensors);
#else
  REQUIRE_FALSE(configuration.has_value());
  CHECK(configuration.error() == ASTL_STATUS_BAD_CONFIGURATION);
#endif
}

TEST_CASE("Saved Windows topology and metrics can be loaded without a native collector", "[windows_collector]") {
  auto configuration = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration.has_value());
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::WindowsSystemTarget>(std::vector<std::string>{"0,0"}));
  astl::Capabilities  capabilities{{astl::CollectorCapability{astl::CollectorType::WINDOWS}},
                                   {astl::SystemCapability{}}};
  astl::MetricManager manager{
      capabilities, {{"cpu", "CPU"}, {"memory", "Memory"}}
  };
  REQUIRE(astl::RegisterWindowsMetrics(*configuration,
                                       {
                                           {astl::CollectorType::WINDOWS, {targets.front().get()}}
  },
                                       &manager) == ASTL_STATUS_SUCCESS);
  std::stringstream metric_stream;
  REQUIRE(astl::ProtobufSerDes::Serialize(static_cast<const astl::IMetricManager&>(manager), metric_stream) ==
          ASTL_STATUS_SUCCESS);
  astl::TopologyManager topology{std::move(targets)};
  std::stringstream     topology_stream;
  REQUIRE(astl::ProtobufSerDes::Serialize(topology, topology_stream) == ASTL_STATUS_SUCCESS);
  auto restored_topology = astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::ITopologyManager>>(topology_stream);
  REQUIRE(restored_topology.has_value());
  const auto& restored_targets = (*restored_topology)->GetTargets();
  REQUIRE(restored_targets.size() == 1);
  CHECK(restored_targets.front()->GetCollectorType() == astl::CollectorType::WINDOWS);
  auto restored_metrics =
      astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::IMetricManager>>(metric_stream, restored_targets);
  REQUIRE(restored_metrics.has_value());
  auto metrics = (*restored_metrics)->GetAvailableMetrics(restored_targets.front().get());
  REQUIRE(metrics.has_value());
  CHECK(metrics->size() == 5);
  configuration->load_file_path = "saved-windows-session.astl";
  CHECK(astl::BuildCollectorManager(restored_targets, *configuration).has_value());
}

#if defined(ASTL_INCLUDE_WINDOWS)
TEST_CASE("Windows live discovery connects target metric and collector builders", "[windows_native]") {
  EnvVarGuard selected(astl::EnvVar::ASTL_COLLECTORS, "windows");
  auto        configuration = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration.has_value());
  auto topology = astl::BuildTopologyManager(*configuration, std::nullopt);
  REQUIRE(topology.has_value());
  const auto& targets = (*topology)->GetTargets();
  REQUIRE(targets.size() == 1);
  CHECK(targets.front()->Name() == "sysutil");
  auto metrics = astl::BuildMetricManager(targets, *configuration, std::nullopt);
  REQUIRE(metrics.has_value());
  auto available = (*metrics)->GetAvailableMetrics(targets.front().get());
  REQUIRE(available.has_value());
  CHECK(available->size() >= 5);
  CHECK(astl::BuildCollectorManager(targets, *configuration).has_value());
}
#endif
