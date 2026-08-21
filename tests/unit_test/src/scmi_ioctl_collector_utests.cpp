// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "collector/scmi_ioctl_collector.hpp"
#include "operation/scmi_read_operation.hpp"
#include "topology/scmi_target.hpp"

namespace fs = std::filesystem;
using trompeloeil::_;

namespace {

struct ScriptedScmiIoctlInterface : public astl::IScmiIoctlInterface {
  std::filesystem::path device_path{"/dev/scmi/tlm_0"};
  scmi_tlm_config       telemetry_config{};
  std::string           de_implementation_version{"00000000000000000000000000000000"};
  uint32_t              data_event_count{};
  bool                  supports_single_read{};
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

  auto DevicePath() const -> const std::filesystem::path& override { return device_path; }

  auto Probe() -> astl_status_code override { return probe_status; }

  auto DeImplementationVersion() -> std::expected<std::string, astl_status_code> override {
    if (probe_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(probe_status);
    }
    return de_implementation_version;
  }

  auto DataEventCount() -> std::expected<uint32_t, astl_status_code> override {
    if (probe_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(probe_status);
    }
    return data_event_count;
  }

  auto SupportsSingleRead() -> std::expected<bool, astl_status_code> override {
    if (probe_status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(probe_status);
    }
    return supports_single_read;
  }

  auto GetConfig(scmi_tlm_config& config) -> astl_status_code override {
    config = telemetry_config;
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
#if defined(__linux__)
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

}  // namespace

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
  REQUIRE(collector.StartCollection() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_BAD_CONFIGURATION);

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
  REQUIRE(collector.StartCollection() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_BAD_CONFIGURATION);
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
  REQUIRE(collector.StartCollection() == ASTL_STATUS_BAD_CONFIGURATION);
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
  CHECK(correlations->at(operation_id).ticks == astl::NativeToMonotonicRawRatio{1'000'000LL, 4});
  REQUIRE(interface.sample_reads.size() == 1);
  CHECK(interface.sample_reads.back() == data_event_id);

  interface.read_data_event_value_status = ASTL_STATUS_FILE_ERROR;
  const auto failed_correlations         = collector.GetNativeClockSnapshot();
  REQUIRE_FALSE(failed_correlations.has_value());
  CHECK(failed_correlations.error() == ASTL_STATUS_FILE_ERROR);
}

TEST_CASE("ScmiIoctlCollector uses advertised V10 single-read support", "[scmi_ioctl_collector]") {
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
