// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "collector/collection_configuration.hpp"
#include "operation/operation.hpp"
#include "procfs/procfs_collector.hpp"
#include "procfs/procfs_operation_builder.hpp"
#include "procfs/procfs_read_operation.hpp"
#include "procfs/procfs_system_utils.hpp"
#include "procfs/procfs_target.hpp"
#include "procfs/procfs_utils_readers.hpp"

namespace fs = std::filesystem;

namespace {

void WriteTextFile(const fs::path& path, std::string_view contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  REQUIRE(!ec);

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  REQUIRE(out.good());
  out << contents;
  REQUIRE(out.good());
}

struct CapturingRawSampleSink : public astl::IRawSampleSink {
  auto SinkRawSamples(const astl::ITarget* target, std::span<astl::RawSampledData> raw_samples)
      -> astl_status_code override {
    last_target = target;
    samples.assign(raw_samples.begin(), raw_samples.end());
    return ASTL_STATUS_SUCCESS;
  }

  const astl::ITarget*              last_target{nullptr};
  std::vector<astl::RawSampledData> samples;
};

struct RecordingRawSampleSink : public astl::IRawSampleSink {
  auto SinkRawSamples(const astl::ITarget* target, std::span<astl::RawSampledData> raw_samples)
      -> astl_status_code override {
    std::scoped_lock lock{mutex};
    last_target = target;
    batches.emplace_back(raw_samples.begin(), raw_samples.end());
    return return_status;
  }

  auto BatchCount() const -> std::size_t {
    std::scoped_lock lock{mutex};
    return batches.size();
  }

  astl_status_code                               return_status{ASTL_STATUS_SUCCESS};
  const astl::ITarget*                           last_target{nullptr};
  std::vector<std::vector<astl::RawSampledData>> batches;
  mutable std::mutex                             mutex;
};

struct NonProcfsOperation : public astl::Operation {};

auto MakeImmediateParams() -> astl_collection_params_t {
  astl_collection_params_t params{};
  params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;
  return params;
}

auto MakeSnapshotParams() -> astl_collection_params_t {
  astl_collection_params_t params{};
  params.collection_mode = ASTL_COLLECTION_MODE_SNAPSHOT;
  return params;
}

auto MakeSamplingParams(uint32_t sampling_interval_ms) -> astl_collection_params_t {
  astl_collection_params_t params{};
  params.collection_mode   = ASTL_COLLECTION_MODE_SAMPLING;
  params.sampling_interval = sampling_interval_ms;
  return params;
}

auto MakeCollectionOperations(astl::OperationSequence before_start = {}, astl::OperationSequence at_start = {},
                              astl::OperationSequence on_sample = {}, astl::OperationSequence at_stop = {})
    -> astl::CollectionOperations {
  return astl::CollectionOperations{
      .operationsBeforeStart{std::move(before_start)},
      .operationsAtStart{std::move(at_start)},
      .operationsOnSample{std::move(on_sample)},
      .operationsAtStop{std::move(at_stop)},
      .samplingInterval{},
      .requirements{astl::CollectorCapability{astl::CollectorType::PROCFS}},
  };
}

}  // namespace

TEST_CASE("ProcfsCollector parses every CPU from one procfs stat snapshot", "[procfs_collector]") {
  constexpr std::string_view contents = R"(cpu  1 2 3 4 5 6 7 8 9 10
cpu0 10 0 5 80 3 2 1 0 0 0
cpu1 20 0 10 60 4 3 2 1 0 0
intr 1 2 3
)";

  const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents(contents);

  REQUIRE(snapshots.has_value());
  REQUIRE(snapshots->size() == 3);
  CHECK(snapshots->at("cpu").total == 36);
  CHECK(snapshots->at("cpu").idle == 9);
  CHECK(snapshots->at("cpu0").total == 101);
  CHECK(snapshots->at("cpu0").idle == 83);
  CHECK(snapshots->at("cpu1").total == 100);
  CHECK(snapshots->at("cpu1").idle == 64);
}

TEST_CASE("Procfs CPU snapshot parsing validates labels and counter values", "[procfs_collector]") {
  SECTION("duplicate CPU labels invalidate the snapshot") {
    const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents("cpu0 1 2 3 4 5\ncpu0 10 20 30 40 50\n");
    REQUIRE_FALSE(snapshots.has_value());
    CHECK(snapshots.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
  SECTION("a CPU line without iowait is valid") {
    const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents("cpu0 1 2 3 4\n");

    REQUIRE(snapshots.has_value());
    REQUIRE(snapshots->size() == 1);
    CHECK(snapshots->at("cpu0").total == 10);
    CHECK(snapshots->at("cpu0").idle == 4);
    CHECK_FALSE(snapshots->at("cpu0").has_iowait);
  }

  SECTION("non-CPU labels are ignored") {
    const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents("cpu-name 1 2 3 4\nintr 1\n");

    REQUIRE_FALSE(snapshots.has_value());
    CHECK(snapshots.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("an incomplete CPU line is rejected") {
    const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents("cpu 1 2 3\n");

    REQUIRE_FALSE(snapshots.has_value());
    CHECK(snapshots.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("a non-numeric CPU counter is rejected") {
    const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents("cpu 1 bad 3 4 5\n");

    REQUIRE_FALSE(snapshots.has_value());
    CHECK(snapshots.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("a requested CPU must exist") {
    const auto snapshot = astl::procfs::detail::ParseCpuSnapshotFromContents("cpu 1 2 3 4 5\n", "cpu1");

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

TEST_CASE("ProcfsCollector reads procfs-backed operations in immediate mode", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_collector";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget     target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector  collector{astl::FileInterface{procfs_root}};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);

  astl_collection_params_t params{};
  params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}));

  astl::CollectionOperations operations{
      .operationsBeforeStart{},
      .operationsAtStart{},
      .operationsOnSample{std::move(on_sample_operations)},
      .operationsAtStop{},
      .samplingInterval{},
      .requirements{astl::CollectorCapability{astl::CollectorType::PROCFS}},
  };

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{&target, std::move(operations), params}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);

  REQUIRE(sink.last_target == &target);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(0.10));
}

TEST_CASE("ProcfsOperationBuilder creates a procfs read operation with the configured descriptor",
          "[procfs_operation_builder]") {
  const astl::procfs::TokenField descriptor{"loadavg", "cpu", 2, ASTL_VALUE_FLOAT64};
  astl::ProcfsOperationBuilder   builder{descriptor};
  astl::ProcfsTarget             target{"procfs", "unit-test procfs target", "/tmp/proc"};

  auto operations_or_error = builder.BuildOperations(&target);

  REQUIRE(operations_or_error.has_value());
  REQUIRE(operations_or_error->size() == 1);

  const auto* operation = dynamic_cast<astl::ProcfsReadOperation*>(operations_or_error->front().get());
  REQUIRE(operation != nullptr);

  const auto* built_descriptor = std::get_if<astl::procfs::TokenField>(&operation->field_descriptor);
  REQUIRE(built_descriptor != nullptr);
  REQUIRE(built_descriptor->relative_path == descriptor.relative_path);
  REQUIRE(built_descriptor->line_prefix == descriptor.line_prefix);
  REQUIRE(built_descriptor->token_index == descriptor.token_index);
  REQUIRE(built_descriptor->raw_value_type == descriptor.raw_value_type);
}

TEST_CASE("ProcfsOperationBuilder ignores the target pointer and still builds the configured operation",
          "[procfs_operation_builder]") {
  const astl::procfs::KeyValueField descriptor{"meminfo", "MemTotal", ASTL_VALUE_UINT64};
  astl::ProcfsOperationBuilder      builder{descriptor};

  auto operations_or_error = builder.BuildOperations(nullptr);

  REQUIRE(operations_or_error.has_value());
  REQUIRE(operations_or_error->size() == 1);

  const auto* operation = dynamic_cast<astl::ProcfsReadOperation*>(operations_or_error->front().get());
  REQUIRE(operation != nullptr);

  const auto* built_descriptor = std::get_if<astl::procfs::KeyValueField>(&operation->field_descriptor);
  REQUIRE(built_descriptor != nullptr);
  REQUIRE(built_descriptor->relative_path == descriptor.relative_path);
  REQUIRE(built_descriptor->field_name == descriptor.field_name);
  REQUIRE(built_descriptor->raw_value_type == descriptor.raw_value_type);
}

TEST_CASE("ProcfsCollector enforces lifecycle state transitions", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_collector_lifecycle";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget    target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector collector{astl::FileInterface{procfs_root}};

  REQUIRE(collector.GetCapabilities().GetCollectorType() == astl::CollectorType::PROCFS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.PauseCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_COLLECTION_NOT_CONFIGURED);

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}));

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);

  astl::OperationSequence replacement_operations;
  replacement_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 1, ASTL_VALUE_FLOAT64}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(replacement_operations), {}),
              MakeImmediateParams()}) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);

  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_COLLECTION_ALREADY_RUNNING);
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{&target, MakeCollectionOperations({}, {}, {}, {}),
                                                                      MakeImmediateParams()}) ==
          ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
}

TEST_CASE("ProcfsCollector rejects unsupported collection modes", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_invalid_mode";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget    target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector collector{astl::FileInterface{procfs_root}};

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}));

  auto params            = MakeImmediateParams();
  params.collection_mode = static_cast<astl_collection_mode_t>(999);

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}), params}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("ProcfsCollector runs snapshot operations at start and stop", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_snapshot";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget     target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector  collector{astl::FileInterface{procfs_root}};
  RecordingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}));
  astl::OperationSequence at_stop_operations;
  at_stop_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 1, ASTL_VALUE_FLOAT64}));

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), std::move(at_stop_operations)),
              MakeSnapshotParams()}) == ASTL_STATUS_SUCCESS);

  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.last_target == &target);
  REQUIRE(sink.batches.size() == 1);
  REQUIRE(sink.batches.front().size() == 1);
  REQUIRE(sink.batches.front().front().get<double>() == Catch::Approx(0.10));

  WriteTextFile(procfs_root / "loadavg", "0.40 0.50 0.60 1/99 1234\n");

  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.batches.size() == 3);
  REQUIRE(sink.batches[1].size() == 1);
  REQUIRE(sink.batches[1].front().get<double>() == Catch::Approx(0.40));
  REQUIRE(sink.batches[2].size() == 1);
  REQUIRE(sink.batches[2].front().get<double>() == Catch::Approx(0.50));
}

TEST_CASE("ProcfsCollector samples periodically and pause resume controls the sampler",
          "[procfs_collector][time_sensitive]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_sampling";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget     target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector  collector{astl::FileInterface{procfs_root}};
  RecordingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}));

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}),
              MakeSamplingParams(10)}) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds{35});
  const auto batches_before_pause = sink.BatchCount();
  REQUIRE(batches_before_pause >= 2);

  REQUIRE(collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds{35});
  const auto batches_during_pause = sink.BatchCount();
  REQUIRE(batches_during_pause == batches_before_pause);

  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds{35});
  REQUIRE(sink.BatchCount() > batches_during_pause);

  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.last_target == &target);
}

TEST_CASE("ProcfsCollector returns errors for invalid operations and procfs read failures", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_collector_errors";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget target{"procfs", "unit-test procfs target", procfs_root};

  SECTION("configure fails when before-start procfs reads fail") {
    astl::ProcfsCollector   collector{astl::FileInterface{procfs_root}};
    astl::OperationSequence before_start_operations;
    before_start_operations.push_back(std::make_unique<astl::ProcfsReadOperation>(
        astl::procfs::TokenField{"missing_file", "", 0, ASTL_VALUE_FLOAT64}));

    REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
                &target, MakeCollectionOperations(std::move(before_start_operations), {}, {}, {}),
                MakeImmediateParams()}) == ASTL_STATUS_FILE_OPEN_FAILED);
  }

  SECTION("read immediate fails when operation is not a procfs read") {
    astl::ProcfsCollector   collector{astl::FileInterface{procfs_root}};
    astl::OperationSequence on_sample_operations;
    on_sample_operations.push_back(std::make_unique<NonProcfsOperation>());

    REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
                &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}),
                MakeImmediateParams()}) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("read immediate fails when procfs input cannot be read") {
    astl::ProcfsCollector   collector{astl::FileInterface{procfs_root}};
    astl::OperationSequence on_sample_operations;
    on_sample_operations.push_back(std::make_unique<astl::ProcfsReadOperation>(
        astl::procfs::TokenField{"missing_file", "", 0, ASTL_VALUE_FLOAT64}));

    REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
                &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}),
                MakeImmediateParams()}) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_FILE_OPEN_FAILED);
  }

  SECTION("read immediate fails when the procfs CPU snapshot cannot be read") {
    astl::ProcfsCollector   collector{astl::FileInterface{procfs_root}};
    astl::OperationSequence on_sample_operations;
    on_sample_operations.push_back(
        std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu"}));

    REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
                &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}),
                MakeImmediateParams()}) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_FILE_OPEN_FAILED);
  }

  SECTION("read immediate fails when the configured CPU is absent") {
    WriteTextFile(procfs_root / "stat", "cpu 1 2 3 4 5 6 7 8\n");

    astl::ProcfsCollector   collector{astl::FileInterface{procfs_root}};
    astl::OperationSequence on_sample_operations;
    on_sample_operations.push_back(
        std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu1"}));

    REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
                &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}),
                MakeImmediateParams()}) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("read immediate fails when a CPU counter is malformed") {
    WriteTextFile(procfs_root / "stat", "cpu 1 invalid 3 4 5 6 7 8\n");

    astl::ProcfsCollector   collector{astl::FileInterface{procfs_root}};
    astl::OperationSequence on_sample_operations;
    on_sample_operations.push_back(
        std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu"}));

    REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
                &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}),
                MakeImmediateParams()}) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

TEST_CASE("ProcfsCollector propagates raw sample sink failures", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_collector_sink_error";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "loadavg", "0.10 0.20 0.30 1/99 1234\n");

  astl::ProcfsTarget     target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector  collector{astl::FileInterface{procfs_root}};
  RecordingRawSampleSink sink;
  sink.return_status = ASTL_STATUS_INTERNAL_ERROR;
  collector.SetRawSampleSink(&sink);

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::TokenField{"loadavg", "", 0, ASTL_VALUE_FLOAT64}));

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(on_sample_operations), {}), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_INTERNAL_ERROR);
  REQUIRE(sink.batches.size() == 1);
}

TEST_CASE("ProcfsCollector computes cpu utilization from atomic /proc/stat snapshots", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_cpu_utilization";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "stat", "cpu  1 2 3 4 5 6 7 8 9 10\n");

  astl::ProcfsTarget     target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector  collector{astl::FileInterface{procfs_root}};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);

  astl_collection_params_t params{};
  params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu"}));

  astl::CollectionOperations operations{
      .operationsBeforeStart{},
      .operationsAtStart{},
      .operationsOnSample{std::move(on_sample_operations)},
      .operationsAtStop{},
      .samplingInterval{},
      .requirements{astl::CollectorCapability{astl::CollectorType::PROCFS}},
  };

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{&target, std::move(operations), params}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);

  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.empty());

  WriteTextFile(procfs_root / "stat", "cpu  2 3 4 8 5 7 8 9 10 9\n");

  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.last_target == &target);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(60.0));

  // No idle ticks during the interval is a legitimate 100% sample.
  WriteTextFile(procfs_root / "stat", "cpu  12 3 4 8 5 7 8 9 10 9\n");

  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(100.0));

  // Total and idle advance together because both counters come from this single snapshot.
  WriteTextFile(procfs_root / "stat", "cpu  12 3 4 108 5 7 8 9 10 9\n");

  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(0.0));

  // Preserve the safe 0% result and exercise diagnostic logging for an impossible snapshot delta.
  WriteTextFile(procfs_root / "stat", "cpu  12 3 4 119 5 7 8 8 10 9\n");

  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(0.0));
}

TEST_CASE("ProcfsCollector computes mem utilization as whole percent", "[procfs_collector]") {
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_mem_utilization";
  TempFileGuard  procfs_guard(procfs_root);

  WriteTextFile(procfs_root / "meminfo", "MemTotal: 1024 kB\nMemAvailable: 256 kB\n");

  astl::ProcfsTarget     target{"procfs", "unit-test procfs target", procfs_root};
  astl::ProcfsCollector  collector{astl::FileInterface{procfs_root}};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);

  astl_collection_params_t params{};
  params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;

  astl::OperationSequence on_sample_operations;
  on_sample_operations.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::MemUsedPercentField{"meminfo"}));

  astl::CollectionOperations operations{
      .operationsBeforeStart{},
      .operationsAtStart{},
      .operationsOnSample{std::move(on_sample_operations)},
      .operationsAtStop{},
      .samplingInterval{},
      .requirements{astl::CollectorCapability{astl::CollectorType::PROCFS}},
  };

  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{&target, std::move(operations), params}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);

  REQUIRE(sink.last_target == &target);
  REQUIRE(sink.samples.size() == 1);
  REQUIRE(sink.samples.front().get<double>() == Catch::Approx(75.0));
}

TEST_CASE("Procfs system parsers preserve device identity and counter units", "[procfs_collector]") {
  const auto          root = fs::temp_directory_path() / "astl_procfs_system_parsers";
  TempFileGuard       guard{root};
  astl::FileInterface files{root};
  WriteTextFile(root / "diskstats", "8 0 sda 10 1 20 0 30 2 40 0 0 0 0\n8 1 sda1 3 0 4 0 5 0 6 0 0 0 0\n");
  WriteTextFile(root / "net/dev",
                "Inter-| Receive | Transmit\n face |bytes packets\n eth0:100 1 2 3 4 5 6 7 200 8 9 10 11 12 13 14\n");
  WriteTextFile(root / "vmstat", "pgfault 1234\npgmajfault 56\n");
  auto read = [&](std::string path, std::string label, std::string kind) {
    return astl::procfs::ReadField(files, astl::procfs::SystemField{path, label, kind});
  };
  REQUIRE(read("diskstats", "sda", "read_operations"));
  CHECK(std::get<uint64_t>(read("diskstats", "sda", "read_operations")->value) == 10);
  CHECK(std::get<uint64_t>(read("diskstats", "sda1", "read_bytes")->value) == 4 * 512);
  CHECK(std::get<uint64_t>(read("diskstats", "sda", "write_bytes")->value) == 40 * 512);
  CHECK(std::get<uint64_t>(read("net/dev", "eth0", "rx_bytes")->value) == 100);
  CHECK(std::get<uint64_t>(read("net/dev", "eth0", "tx_bytes")->value) == 200);
  CHECK(std::get<uint64_t>(read("vmstat", "", "pgmajfault")->value) == 56);
  CHECK_FALSE(read("diskstats", "removed", "read_operations"));
  CHECK_FALSE(read("missing", "", "pgfault"));
  WriteTextFile(root / "diskstats", "8 0 sda 10 0 bad 0 1 0 1 0 0 0 0\n");
  CHECK_FALSE(read("diskstats", "sda", "read_bytes"));
  WriteTextFile(root / "diskstats", "8 0 sda 10 0 18446744073709551615 0 1 0 1 0 0 0 0\n");
  CHECK_FALSE(read("diskstats", "sda", "read_bytes"));
  WriteTextFile(root / "net/dev", "eth0: 1 2\n");
  CHECK_FALSE(read("net/dev", "eth0", "rx_bytes"));
}

TEST_CASE("Procfs swap gauges preserve fractional occupancy and check byte overflow", "[procfs_collector]") {
  const auto          root = fs::temp_directory_path() / "astl_procfs_swap_gauges";
  TempFileGuard       guard{root};
  astl::FileInterface files{root};
  auto                read = [&](std::string kind) {
    return astl::procfs::ReadField(files, astl::procfs::SystemField{"meminfo", "", kind});
  };
  struct SwapCase {
    uint64_t total;
    uint64_t free;
    uint64_t used_bytes;
    double   occupancy;
  };
  const std::array<SwapCase, 5> cases = {
      {
       {100, 25, 76800, 75.0},
       {3, 2, 1024, 100.0 / 3.0},
       {0, 0, 0, 0.0},
       {3, 4, 0, 0.0},
       {UINT64_MAX / 1024, 0, (UINT64_MAX / 1024) * 1024, 100.0},
       }
  };
  for (const auto& input : cases) {
    WriteTextFile(root / "meminfo", "SwapTotal: " + std::to_string(input.total) +
                                        " kB\nSwapFree: " + std::to_string(input.free) + " kB\n");
    auto used      = read("swap_used");
    auto occupancy = read("swap_occupancy");
    REQUIRE(used);
    REQUIRE(occupancy);
    CHECK(std::get<uint64_t>(used->value) == input.used_bytes);
    CHECK(std::get<double>(occupancy->value) == Catch::Approx(input.occupancy));
  }
  WriteTextFile(root / "meminfo", "SwapTotal: 18014398509481984 kB\nSwapFree: 0 kB\n");
  CHECK_FALSE(read("swap_used"));
  WriteTextFile(root / "meminfo", "SwapTotal: -1 kB\nSwapFree: 0 kB\n");
  CHECK_FALSE(read("swap_used"));
  CHECK_FALSE(read("swap_occupancy"));
  // A percentage cannot be used as a cumulative uint64 input for a rate.
  WriteTextFile(root / "meminfo", "SwapTotal: 3 kB\nSwapFree: 2 kB\n");
  astl::ProcfsTarget      target{"procfs", "fixture", root};
  astl::ProcfsCollector   collector{astl::FileInterface{root}};
  astl::OperationSequence sequence;
  sequence.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"meminfo", "", "swap_occupancy", true}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  CHECK(collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  WriteTextFile(root / "meminfo", "SwapTotal: 100 kB\n");
  CHECK_FALSE(read("swap_used"));
  CHECK_FALSE(read("swap_occupancy"));
}

TEST_CASE("Procfs hardware and software interrupt column sums remain separate", "[procfs_collector]") {
  const auto          root = fs::temp_directory_path() / "astl_procfs_interrupt_columns";
  TempFileGuard       guard{root};
  astl::FileInterface files{root};
  WriteTextFile(root / "interrupts",
                " CPU0 CPU2\n 7: 10 20 controller device\n NMI: 3 4 Non-maskable interrupts\n ERR: 999\n Err: 888\n");
  WriteTextFile(root / "softirqs", " CPU0 CPU2\n TIMER: 100 200\n NET_RX: 30 40\n");
  auto read = [&](std::string path, std::string cpu) {
    return astl::procfs::ReadField(files, astl::procfs::SystemField{path, cpu, "interrupts"});
  };
  REQUIRE(read("interrupts", "CPU2"));
  CHECK(std::get<uint64_t>(read("interrupts", "CPU2")->value) == 24);
  CHECK(std::get<uint64_t>(read("softirqs", "CPU0")->value) == 130);
  CHECK_FALSE(read("interrupts", "CPU1"));
  WriteTextFile(root / "softirqs", " CPU0 CPU2\n TIMER: 1 bad\n");
  CHECK_FALSE(read("softirqs", "CPU2"));
  for (const auto* contents : {
           " CPU0 CPU0\n TIMER: 1 2\n",
           " CPU0 CPU2\n TIMER: 1 2\n TIMER: 3 4\n",
           " CPU0 CPU2\n TIMER 1 2\n",
           " CPU0 CPU2\n TIMER: 1\n",
           " CPU0 CPU2\n TIMER: 1 2 3\n",
           " CPU0 CPU2\n TIMER: 1 2 unexpected-suffix\n",
           " CPU0 CPU2\n TIMER: 18446744073709551615 0\n NET_RX: 1 0\n",
       }) {
    WriteTextFile(root / "softirqs", contents);
    CHECK_FALSE(read("softirqs", "CPU0"));
  }
  WriteTextFile(root / "interrupts", "");
  CHECK_FALSE(read("interrupts", "CPU0"));
}

TEST_CASE("Procfs current task counts do not use creation or runnable counters", "[procfs_collector]") {
  const auto          root = fs::temp_directory_path() / "astl_procfs_current_tasks";
  TempFileGuard       guard{root};
  astl::FileInterface files{root};
  WriteTextFile(root / "1/status", "Name: init\nThreads: 3\n");
  WriteTextFile(root / "22/status", "Name: worker\nThreads: 7\n");
  WriteTextFile(root / "stat", "processes 99999\nprocs_running 1\nprocs_blocked 2\n");
  WriteTextFile(root / "loadavg", "0 0 0 1/999 22\n");
  WriteTextFile(root / "self/status", "Threads: 20\n");
  auto read = [&](std::string kind) {
    return astl::procfs::ReadField(files, astl::procfs::SystemField{".", "", kind});
  };
  REQUIRE(read("processes"));
  CHECK(std::get<uint64_t>(read("processes")->value) == 2);
  CHECK(std::get<uint64_t>(read("threads")->value) == 10);
  // Replace the inode, as happens when an exited PID is reused. An open cached
  // descriptor would still read Threads: 7 from the previous task.
  fs::rename(root / "22/status", root / "22/old_status");
  WriteTextFile(root / "22/status", "Threads: 12\n");
  REQUIRE(read("threads"));
  CHECK(std::get<uint64_t>(read("threads")->value) == 15);
  WriteTextFile(root / "22/status", "Name: worker\n");
  CHECK_FALSE(read("threads"));
  fs::remove(root / "22/status");
  CHECK_FALSE(read("threads"));
}

TEST_CASE("Procfs rates baseline on reset and reject nonpositive measured intervals", "[procfs_collector]") {
  astl::procfs::CounterRateState state;
  const auto                     start = astl::procfs::CounterRateState::Clock::time_point{};
  using namespace std::chrono_literals;
  CHECK_FALSE(state.Sample(100, start));
  REQUIRE(state.Sample(130, start + 1500ms));
  CHECK(*state.Sample(160, start + 3s) == Catch::Approx(20));
  CHECK_FALSE(state.Sample(1, start + 4s));
  CHECK(*state.Sample(3, start + 4500ms) == Catch::Approx(4));
  CHECK_FALSE(state.Sample(5, start + 4500ms));
  CHECK_FALSE(state.Sample(6, start + 4s));
  state = {};
  CHECK_FALSE(state.Sample(999, start + 6s));
  CHECK(*state.Sample(999, start + 7s) == Catch::Approx(0));
  state = {};
  CHECK_FALSE(state.Sample(UINT64_MAX, start));
  CHECK_FALSE(state.Sample(0, start + 1s));
  CHECK(*state.Sample(10, start + 3s) == Catch::Approx(5));
}

TEST_CASE("Procfs interrupt column rates use the table acquisition time", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_interrupt_rate_time";
  TempFileGuard guard{root};
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 10 20 controller device\n");
  astl::ProcfsTarget target{"procfs", "fixture", root};
  using Clock                = astl::procfs::CounterRateState::Clock;
  auto                   now = Clock::time_point{};
  astl::ProcfsCollector  collector{astl::FileInterface{root}, [&] {
                                    const auto sampled = now;
                                    now += std::chrono::seconds{1};
                                    return sampled;
                                  }};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  for (const auto* cpu : {"CPU0", "CPU2"}) {
    sequence.push_back(
        std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"interrupts", cpu, "interrupts", true}));
  }
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 20 40 controller device\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<double>() == Catch::Approx(10));
  CHECK(sink.samples[1].get<double>() == Catch::Approx(20));
  sink.samples.clear();
  // A new IRQ row changes the meaning of the column sum even if the sum grows.
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 30 60 controller device\n 2: 1000 2000 new-device\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 40 80 controller device\n 2: 1000 2000 new-device\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<double>() == Catch::Approx(10));
  CHECK(sink.samples[1].get<double>() == Catch::Approx(20));
  sink.samples.clear();
  // A row reset must invalidate the interval even when other rows hide it in the sum.
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 0 0 controller device\n 2: 1100 2200 new-device\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 2 4 controller device\n 2: 1110 2220 new-device\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<double>() == Catch::Approx(12));
  CHECK(sink.samples[1].get<double>() == Catch::Approx(24));
}

TEST_CASE("Procfs I/O wait uses CPU time deltas and re-baselines invalid samples", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_iowait";
  TempFileGuard guard{root};
  WriteTextFile(root / "stat", "cpu0 10 0 10 60 20 0 0 0 100 100\n");
  astl::ProcfsTarget     target{"procfs", "fixture", root};
  astl::ProcfsCollector  collector{astl::FileInterface{root}};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  sequence.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu0", true}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  // 20 extra total ticks, of which 5 are iowait; guest fields are ignored.
  WriteTextFile(root / "stat", "cpu0 15 0 15 65 25 0 0 0 1000 1000\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(sink.samples.front().get<double>() == Catch::Approx(25));
  sink.samples.clear();
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  WriteTextFile(root / "stat", "cpu0 20 0 20 70 24 0 0 0\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  WriteTextFile(root / "stat", "cpu0 25 0 25 75 29 0 0 0\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(sink.samples.front().get<double>() == Catch::Approx(25));
  sink.samples.clear();
  WriteTextFile(root / "stat", "cpu0 30 0 30 80\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  WriteTextFile(root / "stat", "cpu1 1 0 1 1 1 0 0 0\n");
  CHECK(collector.ReadImmediate() != ASTL_STATUS_SUCCESS);
  WriteTextFile(root / "stat", "cpu0 30 0 30 80 34 0 0 0\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
}

TEST_CASE("Procfs rate collection does not bridge observed interface removal", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_interface_lifecycle";
  TempFileGuard guard{root};
  auto          write = [&](uint64_t bytes) {
    WriteTextFile(root / "net/dev", "eth0: " + std::to_string(bytes) + " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");
  };
  write(100);
  astl::ProcfsTarget     target{"procfs", "fixture", root};
  astl::ProcfsCollector  collector{astl::FileInterface{root}};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  sequence.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"net/dev", "eth0", "rx_bytes", true}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  write(200);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(sink.samples.front().get<double>() > 0);
  sink.samples.clear();
  WriteTextFile(root / "net/dev", "lo: 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");
  CHECK(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  write(900);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  write(1);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  write(2);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(sink.samples.front().get<double>() > 0);
}

TEST_CASE("Procfs disk rates re-baseline when a device name changes major minor identity", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_disk_identity";
  TempFileGuard guard{root};
  WriteTextFile(root / "diskstats", "8 0 sda 10 0 20 0 30 0 40 0 0 0 0\n");
  astl::ProcfsTarget target{"procfs", "fixture", root};
  using Clock                = astl::procfs::CounterRateState::Clock;
  auto                   now = Clock::time_point{};
  astl::ProcfsCollector  collector{astl::FileInterface{root}, [&] { return now; }};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  sequence.push_back(std::make_unique<astl::ProcfsReadOperation>(
      astl::procfs::SystemField{"diskstats", "sda", "read_operations", true}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  now += std::chrono::seconds{2};
  WriteTextFile(root / "diskstats", "8 16 sda 1000 0 20 0 30 0 40 0 0 0 0\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  now += std::chrono::seconds{2};
  WriteTextFile(root / "diskstats", "8 16 sda 1010 0 20 0 30 0 40 0 0 0 0\n");
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  CHECK(sink.samples.front().get<double>() == Catch::Approx(5));
}

TEST_CASE("Procfs swap total checks byte conversion without requiring SwapFree", "[procfs_collector]") {
  const auto                      root = fs::temp_directory_path() / "astl_procfs_swap_total";
  TempFileGuard                   guard{root};
  astl::FileInterface             files{root};
  const astl::procfs::SystemField field{"meminfo", "", "swap_total"};
  for (uint64_t total : {uint64_t{0}, uint64_t{3}, UINT64_MAX / 1024}) {
    WriteTextFile(root / "meminfo", "SwapTotal: " + std::to_string(total) + " kB\n");
    auto value = astl::procfs::ReadField(files, field);
    REQUIRE(value);
    CHECK(std::get<uint64_t>(value->value) == total * 1024);
  }
  for (const auto* contents :
       {"SwapFree: 1 kB\n", "SwapTotal: -1 kB\n", "SwapTotal: invalid kB\n", "SwapTotal: 18014398509481984 kB\n"}) {
    WriteTextFile(root / "meminfo", contents);
    CHECK_FALSE(astl::procfs::ReadField(files, field));
  }
}

TEST_CASE("Procfs VM fault rates use measured time and recover from resets and missing input", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_vm_fault_rates";
  TempFileGuard guard{root};
  auto          write = [&](uint64_t faults, uint64_t major) {
    WriteTextFile(root / "vmstat",
                           "pgfault " + std::to_string(faults) + "\npgmajfault " + std::to_string(major) + "\n");
  };
  write(100, 10);
  astl::ProcfsTarget     target{"procfs", "fixture", root};
  auto                   now = astl::procfs::CounterRateState::Clock::time_point{};
  astl::ProcfsCollector  collector{astl::FileInterface{root}, [&] { return now; }};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  for (const auto* kind : {"pgfault", "pgmajfault"}) {
    sequence.push_back(
        std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"vmstat", "", kind, true}));
  }
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  now += std::chrono::milliseconds{1500};
  write(130, 13);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<double>() == Catch::Approx(20));
  CHECK(sink.samples[1].get<double>() == Catch::Approx(2));
  sink.samples.clear();
  now += std::chrono::seconds{1};
  write(1, 1);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  fs::remove(root / "vmstat");
  now += std::chrono::seconds{1};
  CHECK(collector.ReadImmediate() != ASTL_STATUS_SUCCESS);
  write(100, 10);
  now += std::chrono::seconds{1};
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  write(110, 11);
  now += std::chrono::seconds{2};
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<double>() == Catch::Approx(5));
  CHECK(sink.samples[1].get<double>() == Catch::Approx(0.5));
}

TEST_CASE("Procfs task scans restart after an observed PID exit", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_task_retry";
  TempFileGuard guard{root};
  WriteTextFile(root / "1/status", "Threads: 3\n");
  WriteTextFile(root / "2/status", "Threads: 7\n");
  astl::FileInterface files{root};
  size_t              reads       = 0;
  astl_status_code    exit_status = ASTL_STATUS_FILE_OPEN_FAILED;
  SECTION("The status open fails after exit") {}
  SECTION("The status read is empty after exit") { exit_status = ASTL_STATUS_SUCCESS; }
  auto value = astl::procfs::detail::ReadTaskCount(files, true, [&](const auto& path, auto& contents) {
    ++reads;
    if (reads == 2) {
      fs::remove_all(root / path.parent_path());
      contents.clear();
      return exit_status;
    }
    contents = reads == 1 ? "Threads: 10\n" : "Threads: 11\n";
    return ASTL_STATUS_SUCCESS;
  });
  REQUIRE(value);
  CHECK(reads == 3);
  CHECK(std::get<uint64_t>(value->value) == 11);
}

TEST_CASE("Procfs task scan retries are bounded and persistent read failures are not retried", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_task_retry_limits";
  TempFileGuard guard{root};
  WriteTextFile(root / "1/status", "Threads: 3\n");
  astl::FileInterface files{root};
  size_t              reads = 0;
  SECTION("Repeated task churn does not return zero or a partial count") {
    auto value = astl::procfs::detail::ReadTaskCount(files, true, [&](const auto& path, auto&) {
      ++reads;
      fs::remove_all(root / path.parent_path());
      WriteTextFile(root / std::to_string(reads + 1) / "status", "Threads: 3\n");
      return ASTL_STATUS_FILE_OPEN_FAILED;
    });
    CHECK_FALSE(value);
    CHECK(reads == 3);
  }
  SECTION("A still-visible PID with a denied status read is a persistent failure") {
    auto value = astl::procfs::detail::ReadTaskCount(files, true, [&](const auto&, auto&) {
      ++reads;
      return ASTL_STATUS_FILE_OPEN_FAILED;
    });
    REQUIRE_FALSE(value);
    CHECK(value.error() == ASTL_STATUS_FILE_OPEN_FAILED);
    CHECK(reads == 1);
  }
}

TEST_CASE("Unavailable thread counts preserve other procfs samples and rate baselines", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_optional_tasks";
  TempFileGuard guard{root};
  WriteTextFile(root / "1/status", "Threads: 7\n");
  astl::ProcfsTarget     target{"procfs", "fixture", root};
  auto                   now = astl::procfs::CounterRateState::Clock::time_point{};
  astl::ProcfsCollector  collector{astl::FileInterface{root}, [&] { return now; }};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  sequence.push_back(std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{".", "", "threads"}));
  sequence.push_back(std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu"}));
  sequence.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"vmstat", "", "pgfault", true}));
  sequence.push_back(std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"vmstat", "", "pgfault"}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  for (size_t sample = 0; sample < 4; ++sample) {
    WriteTextFile(root / "stat", "cpu " + std::to_string(100 + (sample * 20)) + " 0 0 " +
                                     std::to_string(100 + (sample * 20)) + " 0 0 0 0\n");
    WriteTextFile(root / "vmstat", "pgfault " + std::to_string(100 + (sample * 20)) + "\n");
    if (sample == 1) {
      fs::remove(root / "1/status");
    }
    if (sample == 2) {
      WriteTextFile(root / "1/status", "Name: worker\n");
    }
    if (sample == 3) {
      WriteTextFile(root / "1/status", "Threads: 9\n");
    }
    sink.samples.clear();
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
    if (sample == 0) {
      REQUIRE(sink.samples.size() == 2);
      CHECK(sink.samples[0].get<uint64_t>() == 7);
    } else {
      const size_t offset = sample == 3 ? 1 : 0;
      REQUIRE(sink.samples.size() == 3 + offset);
      CHECK(sink.samples[offset].get<double>() == Catch::Approx(50));
      CHECK(sink.samples[offset + 1].get<double>() == Catch::Approx(20));
      if (sample == 3) {
        CHECK(sink.samples[0].get<uint64_t>() == 9);
      }
    }
    now += std::chrono::seconds{1};
  }
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Procfs missing disk and network labels preserve unrelated samples and baselines", "[procfs_collector]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_io_removal";
  TempFileGuard guard{root};
  std::string   path;
  std::string   label;
  std::string   kind;
  bool          disk = false;
  SECTION("Disk") { disk = true; }
  SECTION("Network") {}
  path       = disk ? "diskstats" : "net/dev";
  label      = disk ? "sda" : "eth0";
  kind       = disk ? "read_operations" : "rx_bytes";
  auto write = [&](uint64_t count, bool present) {
    std::string row =
        disk ? "8 0 " + (present ? label : "sdb") + " " + std::to_string(count) + " 0 20 0 30 0 40 0 0 0 0\n"
             : (present ? label : "eth1") + ": " + std::to_string(count) + " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
    WriteTextFile(root / path, row);
    WriteTextFile(root / "vmstat", "pgfault " + std::to_string(count) + "\n");
    WriteTextFile(root / "stat", "cpu " + std::to_string(count) + " 0 0 " + std::to_string(count) + " 0 0 0 0\n");
  };
  write(100, true);
  astl::ProcfsTarget     target{"procfs", "fixture", root};
  auto                   now = astl::procfs::CounterRateState::Clock::time_point{};
  astl::ProcfsCollector  collector{astl::FileInterface{root}, [&] { return now; }};
  CapturingRawSampleSink sink;
  collector.SetRawSampleSink(&sink);
  astl::OperationSequence sequence;
  for (bool rate : {false, true}) {
    sequence.push_back(std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{path, label, kind, rate}));
  }
  sequence.push_back(
      std::make_unique<astl::ProcfsReadOperation>(astl::procfs::SystemField{"vmstat", "", "pgfault", true}));
  sequence.push_back(std::make_unique<astl::ProcfsReadOperation>(astl::procfs::CpuUtilizationField{"stat", "cpu"}));
  REQUIRE(collector.ConfigureCollection(astl::CollectionConfiguration{
              &target, MakeCollectionOperations({}, {}, std::move(sequence)), MakeImmediateParams()}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StartCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  sink.samples.clear();
  auto sample = [&](uint64_t count, bool present) {
    now += std::chrono::seconds{1};
    write(count, present);
    return collector.ReadImmediate();
  };
  REQUIRE(sample(110, true) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 4);
  CHECK(sink.samples[1].get<double>() == Catch::Approx(10));
  sink.samples.clear();
  REQUIRE(sample(120, false) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 2);
  CHECK(sink.samples[0].get<double>() == Catch::Approx(10));
  CHECK(sink.samples[1].get<double>() == Catch::Approx(50));
  sink.samples.clear();
  REQUIRE(sample(1000, true) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 3);  // Raw I/O resumes immediately; its rate needs a baseline.
  CHECK(sink.samples[0].get<uint64_t>() == 1000);
  CHECK(sink.samples[1].get<double>() == Catch::Approx(880));
  CHECK(sink.samples[2].get<double>() == Catch::Approx(50));
  sink.samples.clear();
  REQUIRE(sample(1010, true) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 4);
  CHECK(sink.samples[1].get<double>() == Catch::Approx(10));
  sink.samples.clear();
  // A malformed selected row is not a missing label and must still fail the pass.
  WriteTextFile(root / path, disk ? "8 0 sda bad\n" : "eth0: bad\n");
  CHECK(collector.ReadImmediate() != ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
  REQUIRE(sample(1020, true) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);  // Pass-wide failures still clear every baseline.
  sink.samples.clear();
  fs::remove(root / path);
  CHECK(collector.ReadImmediate() != ASTL_STATUS_SUCCESS);
  CHECK(sink.samples.empty());
}
