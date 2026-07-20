// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/procfs_collector.hpp"
#include "common/procfs_utils_readers.hpp"
#include "operation/operation.hpp"
#include "operation/procfs_operation_builder.hpp"
#include "operation/procfs_read_operation.hpp"
#include "topology/procfs_target.hpp"

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
  SECTION("a CPU line without iowait is valid") {
    const auto snapshots = astl::procfs::detail::ParseCpuSnapshotsFromContents("cpu0 1 2 3 4\n");

    REQUIRE(snapshots.has_value());
    REQUIRE(snapshots->size() == 1);
    CHECK(snapshots->at("cpu0").total == 10);
    CHECK(snapshots->at("cpu0").idle == 4);
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
  REQUIRE(collector.StartCollection() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_BAD_CONFIGURATION);
  REQUIRE(collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_SUCCESS);

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
