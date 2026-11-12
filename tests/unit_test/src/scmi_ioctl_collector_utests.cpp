// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl_cxx_compat.hpp"
#include "scmi/scmi_clock_correlation.hpp"
#include "scmi/scmi_ioctl_collector.hpp"
#include "scmi/scmi_read_operation.hpp"
#include "scmi/scmi_target.hpp"

namespace fs = std::filesystem;
using trompeloeil::_;

namespace {

struct ScriptedScmiIoctlInterface : public astl::IScmiIoctlInterface {
  std::filesystem::path                                                       device_path{"/dev/scmi/tlm_0"};
  scmi_tlm_config                                                             telemetry_config{};
  std::unordered_map<uint32_t, scmi_tlm_config>                               group_configs;
  std::vector<scmi_tlm_config>                                                config_reads;
  std::function<astl_status_code(astl::ScmiDataEventId, scmi_tlm_de_sample&)> read_hook;
  std::string de_implementation_version{"00000000000000000000000000000000"};
  uint32_t    data_event_count{};
  bool        supports_single_read{};
  std::unordered_map<astl::ScmiDataEventId, scmi_tlm_de_config> data_event_configs;
  std::unordered_map<astl::ScmiDataEventId, scmi_tlm_de_info>   data_event_infos;
  std::deque<scmi_tlm_de_sample>                                samples;
  std::vector<scmi_tlm_config>                                  telemetry_config_writes;
  std::vector<scmi_tlm_de_config>                               data_event_config_writes;
  std::vector<astl::ScmiDataEventId>                            sample_reads;
  std::vector<scmi_tlm_de_sample>                               single_read_samples;
  size_t                                                        single_read_count{};
  astl_status_code                                              get_config_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              set_config_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              probe_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              get_data_event_config_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              set_data_event_config_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              get_data_event_info_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              read_data_event_value_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              read_single_status{ASTL_STATUS_SUCCESS};
  astl_status_code                                              reset_status{ASTL_STATUS_SUCCESS};

  ScriptedScmiIoctlInterface() { telemetry_config.active = {1, -6}; }

  auto DevicePath() const -> const std::filesystem::path& override { return device_path; }

  auto Probe() -> astl_status_code override { return probe_status; }

  auto DeImplementationVersion() -> astl::compat::expected<std::string, astl_status_code> override {
    if (probe_status != ASTL_STATUS_SUCCESS) {
      return astl::compat::unexpected(probe_status);
    }
    return de_implementation_version;
  }

  auto DataEventCount() -> astl::compat::expected<uint32_t, astl_status_code> override {
    if (probe_status != ASTL_STATUS_SUCCESS) {
      return astl::compat::unexpected(probe_status);
    }
    return data_event_count;
  }

  auto SupportsSingleRead() -> astl::compat::expected<bool, astl_status_code> override {
    if (probe_status != ASTL_STATUS_SUCCESS) {
      return astl::compat::unexpected(probe_status);
    }
    return supports_single_read;
  }

  auto GetConfig(scmi_tlm_config& config) -> astl_status_code override {
    config_reads.push_back(config);
    if (SCMI_TLM_CONFIG_IS_GROUP(config.flags) != 0) {
      const auto found = group_configs.find(config.grp_id);
      if (found == group_configs.end()) {
        return ASTL_STATUS_BAD_CONFIGURATION;
      }
      config = found->second;
    } else {
      config = telemetry_config;
    }
    return get_config_status;
  }

  auto SetConfig(scmi_tlm_config& config) -> astl_status_code override {
    config.enable    = config.enable != 0 ? 1U : 0U;
    telemetry_config = config;
    telemetry_config_writes.push_back(config);
    return set_config_status;
  }

  auto GetDataEventConfig(astl::ScmiDataEventId data_event_id, scmi_tlm_de_config& config)
      -> astl_status_code override {
    config    = data_event_configs[data_event_id];
    config.id = data_event_id;
    return get_data_event_config_status;
  }

  auto SetDataEventConfig(scmi_tlm_de_config& config) -> astl_status_code override {
    config.enable                 = config.enable != 0 ? 1U : 0U;
    config.t_enable               = config.t_enable != 0 ? 1U : 0U;
    data_event_configs[config.id] = config;
    data_event_config_writes.push_back(config);
    return set_data_event_config_status;
  }

  auto GetDataEventInfo(astl::ScmiDataEventId data_event_id, scmi_tlm_de_info& info) -> astl_status_code override {
    info    = data_event_infos[data_event_id];
    info.id = data_event_id;
    return get_data_event_info_status;
  }

  auto ReadDataEventValue(astl::ScmiDataEventId data_event_id, scmi_tlm_de_sample& sample)
      -> astl_status_code override {
    sample_reads.push_back(data_event_id);
    if (read_hook) {
      return read_hook(data_event_id, sample);
    }
    if (read_data_event_value_status != ASTL_STATUS_SUCCESS) {
      return read_data_event_value_status;
    }
    if (samples.empty()) {
      sample    = {};
      sample.id = data_event_id;
      return ASTL_STATUS_SUCCESS;
    }
    sample = samples.front();
    samples.pop_front();
    sample.id = data_event_id;
    return ASTL_STATUS_SUCCESS;
  }

  auto ReadSingle(std::span<scmi_tlm_de_sample> output, uint32_t& sample_count) -> astl_status_code override {
    ++single_read_count;
    sample_count = 0;
    if (read_single_status != ASTL_STATUS_SUCCESS) {
      return read_single_status;
    }
    const auto count = std::min(output.size(), single_read_samples.size());
    std::copy_n(single_read_samples.begin(), static_cast<std::ptrdiff_t>(count), output.begin());
    sample_count = static_cast<uint32_t>(count);
    return ASTL_STATUS_SUCCESS;
  }

  auto Reset() -> astl_status_code override { return reset_status; }
};

struct NonScmiOperation : public astl::Operation {};

auto MissingIoctlDeviceStatus() -> astl_status_code {
#ifdef __linux__
  return ASTL_STATUS_NO_TARGET_FOUND;
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

auto MakeCollectionConfiguration(astl::ITarget* target, astl::CollectionOperations operations,
                                 astl_collection_mode_t collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
                                 uint32_t               sampling_interval = 0) -> astl::CollectionConfiguration {
  astl_collection_params_t collection_params{};
  collection_params.size              = sizeof(astl_collection_params_t);
  collection_params.sampling_interval = sampling_interval;
  collection_params.collection_mode   = collection_mode;
  return astl::CollectionConfiguration{target, std::move(operations), collection_params};
}

auto MakeScmiReadOperation(astl::ScmiDataEventId data_event_id) -> std::unique_ptr<astl::ScmiReadOperation> {
  return std::make_unique<astl::ScmiReadOperation>(data_event_id, astl::kilohertz{1});
}

auto MakeDataEventConfig(astl::ScmiDataEventId data_event_id, uint32_t enable, uint32_t tstamp_enable)
    -> scmi_tlm_de_config {
  scmi_tlm_de_config config{};
  config.id       = data_event_id;
  config.enable   = enable;
  config.t_enable = tstamp_enable;
  return config;
}

auto MakeDataEventInfo(astl::ScmiDataEventId data_event_id, uint32_t timestamp_rate) -> scmi_tlm_de_info {
  scmi_tlm_de_info info{};
  info.id      = data_event_id;
  info.ts_rate = timestamp_rate;
  return info;
}

auto MakeSample(astl::ScmiDataEventId data_event_id, uint64_t timestamp, uint64_t value) -> scmi_tlm_de_sample {
  scmi_tlm_de_sample sample{};
  sample.id     = data_event_id;
  sample.tstamp = timestamp;
  sample.val    = value;
  return sample;
}

struct CorrelationFixture {
  EnvVarGuard                                 software_clock{astl::EnvVar::ASTL_SCMI_USE_SOFTWARE_CLOCK_TIMESTAMPS, ""};
  std::unique_ptr<ScriptedScmiIoctlInterface> scripted{std::make_unique<ScriptedScmiIoctlInterface>()};
  ScriptedScmiIoctlInterface&                 interface{*scripted};
  astl::ScmiTarget                            target{"scmi_tlm-0", "correlation target", "tlm-0", nullptr};
  astl::ScmiIoctlCollector                    collector{std::move(scripted)};
  std::vector<astl::OperationId>              operation_ids;

  CorrelationFixture(std::initializer_list<astl::ScmiDataEventId> ids) {
    interface.telemetry_config.enable = 1;
    astl::CollectionOperations operations{.operationsBeforeStart = {},
                                          .operationsAtStart     = {},
                                          .operationsOnSample    = {},
                                          .operationsAtStop      = {},
                                          .samplingInterval      = {},
                                          .requirements = astl::CollectorCapability{astl::CollectorType::SCMI}};
    for (const auto id : ids) {
      interface.data_event_configs[id] = MakeDataEventConfig(id, 1, 1);
      interface.data_event_infos[id]   = MakeDataEventInfo(id, 1'000'000);
      auto operation                   = MakeScmiReadOperation(id);
      operation_ids.push_back(operation->GetId());
      operations.operationsOnSample.push_back(std::move(operation));
    }
    REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
            ASTL_STATUS_SUCCESS);
    interface.config_reads.clear();
  }

  ~CorrelationFixture() { astl::ClockMonotonicRaw::ResetTestTime(); }
  CorrelationFixture(const CorrelationFixture&)                    = delete;
  CorrelationFixture(CorrelationFixture&&)                         = delete;
  auto operator=(const CorrelationFixture&) -> CorrelationFixture& = delete;
  auto operator=(CorrelationFixture&&) -> CorrelationFixture&      = delete;
};

}  // namespace

TEST_CASE("SCMI correlation interleaves twenty attempts per DE and shares mappings for duplicate operations",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture                                 fixture{0x41, 0x42, 0x41};
  std::unordered_map<astl::ScmiDataEventId, int64_t> attempts;
  fixture.interface.read_hook = [&](auto de_id, auto& sample) {
    const auto attempt = attempts[de_id]++;
    sample             = MakeSample(de_id, static_cast<uint64_t>(100 + (attempt * 100)), 42);
    astl::ClockMonotonicRaw::SetTestTime(
        astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1000 + (attempt * 150)}});
    return ASTL_STATUS_SUCCESS;
  };
  const auto result = fixture.collector.GetNativeClockSnapshot();
  REQUIRE(result);
  REQUIRE(result->size() == 3);
  REQUIRE(fixture.interface.sample_reads.size() == 40);
  for (size_t attempt = 0; attempt < 20; ++attempt) {
    CHECK(fixture.interface.sample_reads[attempt * 2] == 0x41);
    CHECK(fixture.interface.sample_reads[(attempt * 2) + 1] == 0x42);
  }
  for (const auto id : fixture.operation_ids) {
    const auto& correlation = result->at(id);
    CHECK(correlation.native_at_start == 100);
    CHECK(correlation.raw_monotonic_at_start.time_since_epoch().count() == 1000);
  }
  CHECK(result->at(fixture.operation_ids[0]) == result->at(fixture.operation_ids[2]));
  REQUIRE(fixture.interface.config_reads.size() == 1);
  CHECK(fixture.interface.config_reads.front().flags == 0);
}

TEST_CASE("SCMI correlation selects the minimum from available timestamps after duplicate and failed attempts",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture fixture{0x41};
  int64_t            attempt  = 0;
  fixture.interface.read_hook = [&](auto de_id, auto& sample) {
    const auto index = attempt++;
    astl::ClockMonotonicRaw::SetTestTime(
        astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1000 + (index * 100)}});
    sample = MakeSample(de_id, 500, 42);
    return index == 1 || index == 7 ? ASTL_STATUS_FILE_ERROR : ASTL_STATUS_SUCCESS;
  };
  const auto result = fixture.collector.GetNativeClockSnapshot();
  REQUIRE(result);
  CHECK(attempt == 20);
  CHECK(result->at(fixture.operation_ids[0]).raw_monotonic_at_start.time_since_epoch().count() == 1000);
  CHECK(result->at(fixture.operation_ids[0]).native_at_start == 500);
}

TEST_CASE("SCMI correlation rejects all failed reads, zero timestamps, and backward clocks",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture fixture{0x41};
  SECTION("all attempts fail") {
    fixture.interface.read_data_event_value_status = ASTL_STATUS_FILE_ERROR;
    const auto result                              = fixture.collector.GetNativeClockSnapshot();
    REQUIRE_FALSE(result);
    CHECK(result.error() == ASTL_STATUS_FILE_ERROR);
    CHECK(fixture.interface.sample_reads.size() == 20);
  }
  SECTION("zero timestamps are unusable") {
    const auto result = fixture.collector.GetNativeClockSnapshot();
    REQUIRE_FALSE(result);
    CHECK(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
    CHECK(fixture.interface.sample_reads.size() == 20);
  }
  SECTION("backward clock invalidates the mapping") {
    fixture.interface.samples.push_back(MakeSample(0x41, 100, 42));
    fixture.interface.samples.push_back(MakeSample(0x41, 99, 42));
    const auto result = fixture.collector.GetNativeClockSnapshot();
    REQUIRE_FALSE(result);
    CHECK(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
    CHECK(fixture.interface.sample_reads.size() == 2);
  }
}

TEST_CASE("SCMI minimum uses a single valid observation without replacement attempts",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture fixture{0x41};
  size_t             attempt  = 0;
  fixture.interface.read_hook = [&](auto de_id, auto& sample) {
    ++attempt;
    sample = MakeSample(de_id, attempt == 10 ? 500 : 0, 42);
    astl::ClockMonotonicRaw::SetTestTime(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1000}});
    return attempt % 2 == 0 ? ASTL_STATUS_SUCCESS : ASTL_STATUS_FILE_ERROR;
  };
  const auto result = fixture.collector.GetNativeClockSnapshot();
  REQUIRE(result);
  CHECK(attempt == 20);
  CHECK(result->at(fixture.operation_ids[0]).native_at_start == 500);
  CHECK(result->at(fixture.operation_ids[0]).raw_monotonic_at_start.time_since_epoch().count() == 1000);
}

TEST_CASE("SCMI backward timestamps terminate interleaved sampling immediately",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture fixture{0x41, 0x42};
  size_t             attempt  = 0;
  fixture.interface.read_hook = [&](auto de_id, auto& sample) {
    sample = MakeSample(de_id, ++attempt == 3 ? 99 : 100, 42);
    return ASTL_STATUS_SUCCESS;
  };
  const auto result = fixture.collector.GetNativeClockSnapshot();
  REQUIRE_FALSE(result);
  CHECK(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  CHECK(fixture.interface.sample_reads == std::vector<astl::ScmiDataEventId>{0x41, 0x42, 0x41});
}

TEST_CASE("SCMI correlation reads target and group intervals and respects their spacing",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture fixture{0x41, 0x42};
  fixture.interface.telemetry_config.active       = {1200, -6};
  fixture.interface.data_event_infos[0x42].flags  = SCMI_TLM_DEINFO_GROUP;
  fixture.interface.data_event_infos[0x42].grp_id = 7;
  auto& group_config                              = fixture.interface.group_configs[7];
  group_config.enable                             = 1;
  group_config.flags                              = SCMI_TLM_CONFIG_GROUP;
  group_config.grp_id                             = 7;
  group_config.active                             = {2, -3};
  std::chrono::microseconds minimum_spacing{};
  SECTION("group interval limits polling") { minimum_spacing = std::chrono::microseconds{1000}; }
  SECTION("target interval limits polling") {
    fixture.interface.telemetry_config.active = {4, -3};
    minimum_spacing                           = std::chrono::microseconds{2000};
  }
  std::unordered_map<astl::ScmiDataEventId, size_t> observations;
  fixture.interface.read_hook = [&](auto de_id, auto& sample) {
    sample = MakeSample(de_id, ++observations[de_id], 42);
    return ASTL_STATUS_SUCCESS;
  };
  const auto start = std::chrono::steady_clock::now();
  REQUIRE(fixture.collector.GetNativeClockSnapshot());
  const auto elapsed = std::chrono::steady_clock::now() - start;
  // The collector spaces read starts, before entering the mock. Scheduler pauses
  // or instrumentation between those points can shorten mock-entry deltas by an
  // arbitrary amount. Bracket the full capture instead: 20 reads require at least
  // 19 spacings for the slower event, regardless of where execution is delayed.
  CHECK(elapsed >= 19 * minimum_spacing);
  REQUIRE(fixture.interface.config_reads.size() == 2);
  CHECK(fixture.interface.config_reads[0].flags == 0);
  CHECK(fixture.interface.config_reads[1].flags == SCMI_TLM_CONFIG_GROUP);
  CHECK(fixture.interface.config_reads[1].grp_id == 7);
  REQUIRE(observations.size() == 2);
  CHECK(observations.at(0x41) == 20);
  CHECK(observations.at(0x42) == 20);
}

TEST_CASE("SCMI correlation rejects unavailable or invalid active intervals before sampling",
          "[scmi_ioctl_collector][scmi_clock_correlation]") {
  CorrelationFixture fixture{0x41};
  SECTION("configuration read fails") { fixture.interface.get_config_status = ASTL_STATUS_FILE_ERROR; }
  SECTION("active interval is zero") { fixture.interface.telemetry_config.active = {0, -6}; }
  SECTION("active interval overflows") { fixture.interface.telemetry_config.active = {1, 100}; }
  SECTION("group configuration is unavailable") {
    fixture.interface.data_event_infos[0x41].flags  = SCMI_TLM_DEINFO_GROUP;
    fixture.interface.data_event_infos[0x41].grp_id = 7;
  }
  CHECK_FALSE(fixture.collector.GetNativeClockSnapshot());
  CHECK(fixture.interface.sample_reads.empty());
}

TEST_CASE("ScmiIoctlCollector reports capabilities and handles unconfigured lifecycle calls",
          "[scmi_ioctl_collector]") {
  const fs::path  base_path      = fs::temp_directory_path() / "astl_missing_scmi_ioctl_collector_test";
  const fs::path  missing_device = base_path / "tlm_0";
  TempFileGuard   cleanup(base_path);
  std::error_code ec;
  fs::remove_all(base_path, ec);
  REQUIRE(!ec);

  astl::ScmiIoctlCollector collector{missing_device};
  MockRawSampleSink        raw_sample_sink;
  collector.SetRawSampleSink(&raw_sample_sink);

  REQUIRE(collector.GetCapabilities().collector_type == astl::CollectorType::SCMI);
  REQUIRE(collector.ClearCollectionState() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.PauseCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);

  auto clock_snapshot = collector.GetNativeClockSnapshot();
  REQUIRE(clock_snapshot.has_value());
  REQUIRE(clock_snapshot->empty());

  collector.SetRawSampleSink(nullptr);
}

TEST_CASE("ScmiIoctlCollector rolls back configuration when telemetry enable fails", "[scmi_ioctl_collector]") {
  const fs::path  base_path      = fs::temp_directory_path() / "astl_missing_scmi_ioctl_collector_configure_test";
  const fs::path  missing_device = base_path / "tlm_0";
  TempFileGuard   cleanup(base_path);
  std::error_code ec;
  fs::remove_all(base_path, ec);
  REQUIRE(!ec);

  astl::ScmiIoctlCollector collector{missing_device};
  astl::ScmiTarget         target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  astl_collection_params_t collection_params{};
  collection_params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;

  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(
      std::make_unique<astl::ScmiReadOperation>(astl::ScmiDataEventId{0x1234U}, astl::kilohertz{1}));

  astl::CollectionConfiguration configuration{&target, std::move(operations), collection_params};

  REQUIRE(collector.ConfigureCollection(std::move(configuration)) == MissingIoctlDeviceStatus());
  REQUIRE(collector.StartCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
}

TEST_CASE("ScmiIoctlCollector rolls back configuration when a pre-start operation fails", "[scmi_ioctl_collector]") {
  auto  scripted_interface          = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface                   = *scripted_interface;
  interface.telemetry_config.enable = 1;

  constexpr astl::ScmiDataEventId data_event_id{0x1234U};
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 1);

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsBeforeStart.push_back(std::make_unique<NonScmiOperation>());
  operations.operationsOnSample.push_back(MakeScmiReadOperation(data_event_id));

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_BAD_ARGUMENT);

  REQUIRE(interface.data_event_config_writes.size() == 2);
  CHECK(interface.data_event_config_writes.front().enable == 1);
  CHECK(interface.data_event_config_writes.back().enable == 0);
  CHECK(interface.data_event_configs[data_event_id].enable == 0);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("ScmiIoctlCollector configures data events, reads immediate samples, and restores state",
          "[scmi_ioctl_collector]") {
  auto  scripted_interface            = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface                     = *scripted_interface;
  interface.telemetry_config.enable   = 0;
  interface.de_implementation_version = "CAFE0000000000000000000000000000";

  constexpr astl::ScmiDataEventId data_event_id{0x1234U};
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 4);
  interface.samples.push_back(MakeSample(data_event_id, 1000, 0x42));
  interface.samples.push_back(MakeSample(data_event_id, 1000, 0x43));
  interface.samples.push_back(MakeSample(data_event_id, 1001, 0x44));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  auto                       read_operation = MakeScmiReadOperation(data_event_id);
  const auto                 operation_id   = read_operation->GetId();
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(std::move(read_operation));

  MockRawSampleSink                 mock_raw_sample_sink;
  std::vector<astl::RawSampledData> received_samples;
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .TIMES(4)
      .LR_SIDE_EFFECT((received_samples.insert(received_samples.end(), _2.begin(), _2.end())))
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  collector.SetRawSampleSink(&mock_raw_sample_sink);

  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(interface.telemetry_config_writes.size() == 1);
  REQUIRE(interface.telemetry_config.enable == 1);
  REQUIRE(interface.data_event_config_writes.size() == 1);
  REQUIRE(interface.data_event_config_writes.back().enable == 1);
  REQUIRE(interface.data_event_config_writes.back().t_enable == 1);

  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);

  REQUIRE(received_samples.size() == 4);
  CHECK(received_samples[0].operation_id == astl::kPauseResumeOperationId);
  CHECK(std::get<uint64_t>(received_samples[0].value.value) == 0);
  CHECK(received_samples[1].operation_id == operation_id);
  CHECK(received_samples[1].value == astl::AstlValue{uint64_t{0x42}});
  CHECK(received_samples[1].raw_tick == 1000);
  CHECK(received_samples[2].operation_id == astl::kPauseResumeOperationId);
  CHECK(std::get<uint64_t>(received_samples[2].value.value) == 1);
  CHECK(received_samples[3].operation_id == operation_id);
  CHECK(received_samples[3].value == astl::AstlValue{uint64_t{0x44}});
  CHECK(received_samples[3].raw_tick == 1001);

  REQUIRE(interface.data_event_config_writes.size() == 2);
  CHECK(interface.data_event_config_writes.back().enable == 0);
  CHECK(interface.data_event_config_writes.back().t_enable == 0);
  CHECK(interface.data_event_configs[data_event_id].enable == 0);
  CHECK(interface.data_event_configs[data_event_id].t_enable == 0);
}

TEST_CASE("ScmiIoctlCollector snapshots native clocks and reports ioctl read failures", "[scmi_ioctl_collector]") {
  auto  scripted_interface = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface          = *scripted_interface;

  constexpr astl::ScmiDataEventId data_event_id{0x5678U};
  interface.telemetry_config.enable           = 1;
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 1, 1);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 4);
  interface.samples.push_back(MakeSample(data_event_id, 2500, 0xAA));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  auto                       read_operation = MakeScmiReadOperation(data_event_id);
  const auto                 operation_id   = read_operation->GetId();
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(std::make_unique<NonScmiOperation>());
  operations.operationsOnSample.push_back(std::move(read_operation));

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_SUCCESS);

  const auto correlations = collector.GetNativeClockSnapshot();
  REQUIRE(correlations.has_value());
  REQUIRE(correlations->count(operation_id) == 1);
  CHECK(correlations->at(operation_id).native_at_start == 2500);
  CHECK(correlations->at(operation_id).ticks == astl::NativeToMonotonicRawRatio{250'000, 1});
  REQUIRE(interface.sample_reads.size() == astl::scmi_detail::kClockCorrelationReadAttempts);
  CHECK(interface.sample_reads.back() == data_event_id);

  interface.read_data_event_value_status = ASTL_STATUS_FILE_ERROR;
  const auto failed_correlations         = collector.GetNativeClockSnapshot();
  REQUIRE_FALSE(failed_correlations.has_value());
  CHECK(failed_correlations.error() == ASTL_STATUS_FILE_ERROR);
}

TEST_CASE("ScmiIoctlCollector uses advertised SCMI telemetry ABI V1 single-read support for the V10 patch set or later",
          "[scmi_ioctl_collector]") {
  auto  scripted_interface = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface          = *scripted_interface;

  constexpr astl::ScmiDataEventId data_event_id{0x4567U};
  interface.supports_single_read              = true;
  interface.data_event_count                  = 1;
  interface.telemetry_config.enable           = 1;
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 4);
  interface.single_read_samples.push_back(MakeSample(data_event_id, 3000, 0xBB));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(MakeScmiReadOperation(data_event_id));

  MockRawSampleSink mock_raw_sample_sink;
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .WITH(_2.size() == 1 && _2.front().raw_tick == 3000)
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  collector.SetRawSampleSink(&mock_raw_sample_sink);
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(interface.single_read_count == 1);
  CHECK(interface.sample_reads.empty());
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("ScmiIoctlCollector uses the default counter rate when ioctl reports zero", "[scmi_ioctl_collector]") {
  auto  scripted_interface = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface          = *scripted_interface;

  constexpr astl::ScmiDataEventId data_event_id{0x5678U};
  interface.telemetry_config.enable           = 1;
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 0);
  for (size_t attempt = 0; attempt < astl::scmi_detail::kClockCorrelationReadAttempts; ++attempt) {
    interface.samples.push_back(MakeSample(data_event_id, 1'000'000'000, 0xAA));
  }
  interface.samples.push_back(MakeSample(data_event_id, 1'000'000'100, 0xBB));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  auto                       read_operation = MakeScmiReadOperation(data_event_id);
  const auto                 operation_id   = read_operation->GetId();
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(std::move(read_operation));

  MockRawSampleSink                 mock_raw_sample_sink;
  std::vector<astl::RawSampledData> received_samples;
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .WITH(_2.size() == 1)
      .LR_SIDE_EFFECT((received_samples.insert(received_samples.end(), _2.begin(), _2.end())))
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  collector.SetRawSampleSink(&mock_raw_sample_sink);
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(interface.data_event_config_writes.size() == 1);
  CHECK(interface.data_event_config_writes.back().t_enable == 1);

  const auto correlations = collector.GetNativeClockSnapshot();
  REQUIRE(correlations.has_value());
  REQUIRE(correlations->count(operation_id) == 1);
  CHECK(correlations->at(operation_id).native_at_start == 1'000'000'000);
  CHECK(correlations->at(operation_id).ticks == astl::NativeToMonotonicRawRatio{1, 1});

  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(received_samples.size() == 1);
  CHECK(received_samples[0].raw_tick == 1'000'000'100);
  CHECK(received_samples[0].value == astl::AstlValue{uint64_t{0xBB}});
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(interface.data_event_config_writes.size() == 2);
  CHECK(interface.data_event_config_writes.back().t_enable == 0);
}

TEST_CASE("ScmiIoctlCollector uses software-clock timestamps when requested", "[scmi_ioctl_collector]") {
  EnvVarGuard env_cleanup{astl::EnvVar::ASTL_SCMI_USE_SOFTWARE_CLOCK_TIMESTAMPS, "1"};

  auto  scripted_interface = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface          = *scripted_interface;

  constexpr astl::ScmiDataEventId data_event_id{0x9ABCU};
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 1000);
  interface.samples.push_back(MakeSample(data_event_id, 1234567890, 0x55));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  auto                       read_operation = MakeScmiReadOperation(data_event_id);
  const auto                 operation_id   = read_operation->GetId();
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(std::move(read_operation));

  MockRawSampleSink                 mock_raw_sample_sink;
  std::vector<astl::RawSampledData> received_samples;
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .WITH(_2.size() == 1)
      .LR_SIDE_EFFECT((received_samples.insert(received_samples.end(), _2.begin(), _2.end())))
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  collector.SetRawSampleSink(&mock_raw_sample_sink);
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(interface.data_event_config_writes.size() == 1);
  CHECK(interface.data_event_config_writes.back().enable == 1);
  CHECK(interface.data_event_config_writes.back().t_enable == 0);

  const auto read_count_before_snapshot = interface.sample_reads.size();
  const auto before_snapshot = static_cast<uint64_t>(astl::ClockMonotonicRaw::now().time_since_epoch().count());
  const auto correlations    = collector.GetNativeClockSnapshot();
  const auto after_snapshot  = static_cast<uint64_t>(astl::ClockMonotonicRaw::now().time_since_epoch().count());
  REQUIRE(correlations.has_value());
  REQUIRE(correlations->count(operation_id) == 1);
  CHECK(correlations->at(operation_id).ticks == astl::NativeToMonotonicRawRatio{1, 1});
  CHECK(correlations->at(operation_id).native_at_start >= before_snapshot);
  CHECK(correlations->at(operation_id).native_at_start <= after_snapshot);
  CHECK(interface.sample_reads.size() == read_count_before_snapshot);

  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  const auto before_read = static_cast<uint64_t>(astl::ClockMonotonicRaw::now().time_since_epoch().count());
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  const auto after_read = static_cast<uint64_t>(astl::ClockMonotonicRaw::now().time_since_epoch().count());
  REQUIRE(received_samples.size() == 1);
  CHECK(received_samples[0].operation_id == operation_id);
  CHECK(received_samples[0].value == astl::AstlValue{uint64_t{0x55}});
  CHECK(received_samples[0].raw_tick != uint64_t{1234567890});
  CHECK(received_samples[0].raw_tick >= before_read);
  CHECK(received_samples[0].raw_tick <= after_read);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("ScmiIoctlCollector executes snapshot mode at start and stop", "[scmi_ioctl_collector]") {
  auto  scripted_interface = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface          = *scripted_interface;

  constexpr astl::ScmiDataEventId data_event_id{0x7777U};
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 1);
  interface.samples.push_back(MakeSample(data_event_id, 10, 0x10));
  interface.samples.push_back(MakeSample(data_event_id, 20, 0x20));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(MakeScmiReadOperation(data_event_id));

  MockRawSampleSink                 mock_raw_sample_sink;
  std::vector<astl::RawSampledData> received_samples;
  REQUIRE_CALL(mock_raw_sample_sink, SinkRawSamples(_, _))
      .TIMES(2)
      .LR_SIDE_EFFECT((received_samples.insert(received_samples.end(), _2.begin(), _2.end())))
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  collector.SetRawSampleSink(&mock_raw_sample_sink);
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(
              &target, std::move(operations), ASTL_COLLECTION_MODE_SNAPSHOT)) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);

  REQUIRE(received_samples.size() == 2);
  CHECK(received_samples[0].value == astl::AstlValue{uint64_t{0x10}});
  CHECK(received_samples[0].raw_tick == 10);
  CHECK(received_samples[1].value == astl::AstlValue{uint64_t{0x20}});
  CHECK(received_samples[1].raw_tick == 20);
}

TEST_CASE("ScmiIoctlCollector starts and stops sampling mode", "[scmi_ioctl_collector]") {
  auto  scripted_interface = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface          = *scripted_interface;

  constexpr astl::ScmiDataEventId data_event_id{0x8888U};
  interface.data_event_configs[data_event_id] = MakeDataEventConfig(data_event_id, 0, 0);
  interface.data_event_infos[data_event_id]   = MakeDataEventInfo(data_event_id, 1);
  interface.samples.push_back(MakeSample(data_event_id, 30, 0x30));

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsOnSample.push_back(MakeScmiReadOperation(data_event_id));

  MockRawSampleSink mock_raw_sample_sink;
  ALLOW_CALL(mock_raw_sample_sink, SinkRawSamples(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  collector.SetRawSampleSink(&mock_raw_sample_sink);
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(
              &target, std::move(operations), ASTL_COLLECTION_MODE_SAMPLING, 1000)) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("ScmiIoctlCollector rejects non-SCMI operations during execution", "[scmi_ioctl_collector]") {
  auto  scripted_interface          = std::make_unique<ScriptedScmiIoctlInterface>();
  auto& interface                   = *scripted_interface;
  interface.telemetry_config.enable = 1;

  astl::ScmiTarget           target{"scmi_tlm-0", "unit-test target", "tlm-0", nullptr};
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = astl::SamplingInterval{std::chrono::milliseconds{100}},
                                        .requirements          = astl::CollectorCapability{astl::CollectorType::SCMI}};
  operations.operationsAtStart.push_back(std::make_unique<NonScmiOperation>());

  astl::ScmiIoctlCollector collector{std::move(scripted_interface)};
  REQUIRE(collector.ConfigureCollection(MakeCollectionConfiguration(&target, std::move(operations))) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(collector.ClearCollectionState() == ASTL_STATUS_SUCCESS);
}
