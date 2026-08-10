// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>

#include "../../mock_classes.hpp"
#include "../../test_utilities.hpp"
#include "astl/astl_errors.h"
#include "capabilities.hpp"
#include "metric/delta_metric.hpp"
#include "metric/event_metric.hpp"
#include "metric/finite_set_metric.hpp"
#include "metric/metric_manager.hpp"
#include "metric/procfs_composite_metricconfig.hpp"
#include "metric/rate_metric.hpp"
#include "metric/sampled_value_metric.hpp"
#include "operation/scmi_operation_builder.hpp"
#include "orchestrator/orchestrator.hpp"
#include "serdes/metrics.pb.h"
#include "serdes/protobuf_serdes.hpp"
#include "serdes/raw_samples.pb.h"  // AUTO-GENERATED RawSampleBatch
#include "serdes/targets.pb.h"      // AUTO-GENERATED Target
#include "topology/i_topology_manager.hpp"
#include "topology/scmi_target.hpp"
#include "topology/topology_manager.hpp"

namespace fs = std::filesystem;

using astl::AstlValue;
using astl::OperationId;
using astl::RawSampledData;
using astl::SampleTimestamp;

using astl::ProtobufSerDes::Deserialize;
using astl::ProtobufSerDes::Serialize;

namespace {

RawSampledData MakeSample(OperationId operation_id, AstlValue value, int64_t ts_us) {
  RawSampledData sample{operation_id, std::move(value)};
  sample.raw_tick = static_cast<uint64_t>(ts_us);
  return sample;
}

auto MakeTarget(std::string name, std::string description, astl::CollectorType collector_type,
                std::optional<std::string> uuid                = std::nullopt,
                std::optional<std::string> collector_path_name = std::nullopt) -> std::unique_ptr<astl::Target> {
  return std::make_unique<astl::Target>(std::move(name), std::move(description), collector_type, nullptr,
                                        std::move(uuid), std::move(collector_path_name));
}

// Helper to install a single SCMI target named "tlm-0" into the Orchestrator
const astl::ITarget* InstallSingleScmiTargetTlm0() {
  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());

  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(
      MakeTarget("tlm-0", "unit-test target", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
  REQUIRE(orch->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  const auto& current_targets = orch->GetTargets();
  REQUIRE_FALSE(current_targets.empty());
  REQUIRE(current_targets[0] != nullptr);
  REQUIRE(current_targets[0]->Name() == "tlm-0");

  return current_targets[0].get();
}

astl::protobuf::MetricManager BuildValidMetricManagerProto() {
  astl::protobuf::MetricManager proto_mgr;

  // Capabilities: expose SCMI collector
  proto_mgr.add_capabilities(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

  // Metrics: one ASTL_METRIC_VALUE bound to target "tlm-0"
  auto* proto_metrics_vec = proto_mgr.mutable_metrics();
  auto* raw               = proto_metrics_vec->add_metrics();
  raw->set_metric_id("test_metric");
  raw->add_target_ids("tlm-0");

  auto* cfg = raw->mutable_config();
  cfg->set_metric_name("test_metric");
  cfg->set_description("unit-test metric");
  cfg->set_units(static_cast<astl::protobuf::AstlUnits>(ASTL_UNITS_CELSIUS));
  cfg->set_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
  cfg->set_input_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
  cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_VALUE));
  cfg->set_identifier(static_cast<astl::protobuf::AstlMetricIdentifier>(ASTL_METRIC_IDENTIFIER_UNKNOWN));
  cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

  return proto_mgr;
}

auto AddCounterToMetricManagerProto(astl::protobuf::MetricManager& proto_mgr, std::string_view counter_id = "counter")
    -> astl::protobuf::RawMetric* {
  auto* raw = proto_mgr.mutable_counters()->add_metrics();
  raw->set_metric_id(std::string{counter_id});
  raw->add_target_ids("tlm-0");

  auto* cfg = raw->mutable_config();
  cfg->set_metric_name(std::string{counter_id});
  cfg->set_description("unit-test counter");
  cfg->set_units(static_cast<astl::protobuf::AstlUnits>(ASTL_UNITS_NONE));
  cfg->set_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
  cfg->set_input_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
  cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_VALUE));
  cfg->set_identifier(static_cast<astl::protobuf::AstlMetricIdentifier>(ASTL_METRIC_IDENTIFIER_UNKNOWN));
  cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

  return raw;
}

auto MakeCaps(astl::CollectorType collector_type) -> astl::Capabilities {
  std::vector<astl::CollectorCapability> collector_caps{astl::CollectorCapability{collector_type}};
  std::vector<astl::SystemCapability>    system_caps{astl::SystemCapability{}};
  return astl::Capabilities{std::move(collector_caps), std::move(system_caps)};
}

}  // namespace

// NOLINTBEGIN(readability-magic-numbers,readability-function-cognitive-complexity)
TEST_CASE("Serialize/Deserialize round-trip for all supported scalar types") {
  std::vector<RawSampledData> input;
  input.push_back(MakeSample(1, AstlValue{uint8_t{7}}, 10));
  input.push_back(MakeSample(2, AstlValue{uint16_t{1024}}, 20));
  input.push_back(MakeSample(3, AstlValue{uint32_t{424242}}, 30));
  input.push_back(MakeSample(4, AstlValue{uint64_t{9000000000ULL}}, 40));
  input.push_back(MakeSample(5, AstlValue{3.5F}, 50));
  input.push_back(MakeSample(6, AstlValue{6.25}, 60));
  input.push_back(MakeSample(7, AstlValue{true}, 70));

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(input, str_stream) == ASTL_STATUS_SUCCESS);

  str_stream.seekg(0);
  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(str_stream);
  REQUIRE(out_or.has_value());
  const auto& out = *out_or;

  REQUIRE(out.size() == input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    REQUIRE(out[i].operation_id == input[i].operation_id);
    REQUIRE(out[i].raw_tick == input[i].raw_tick);
    REQUIRE(out[i].value == input[i].value);
  }
}
// NOLINTEND(readability-magic-numbers,readability-function-cognitive-complexity)

TEST_CASE("SerializeCurrentBatch returns NO_DATA_COLLECTED on empty input") {
  std::vector<RawSampledData> empty;
  REQUIRE(astl::ProtobufSerDes::SerializeCurrentBatch("serdes_empty_test", empty, "") == ASTL_STATUS_NO_DATA_COLLECTED);
}

// NOLINTBEGIN(readability-magic-numbers,readability-function-cognitive-complexity)
TEST_CASE("SerializeCurrentBatch writes one batch that Deserialize can read") {
  // Unique target file
  std::random_device                      random_device;
  std::mt19937                            gen(random_device());
  std::uniform_int_distribution<uint64_t> dis;
  const auto                              rand      = std::to_string(dis(gen));
  const fs::path                          dir       = "tmp";
  const auto                              file_path = dir / ("serdes_on_disk_test_" + rand + ".astl");
  TempFileGuard                           guard{dir};

  // Clean slate
  std::error_code err_code;
  fs::create_directories(file_path.parent_path(), err_code);
  fs::remove(file_path, err_code);

  std::vector<RawSampledData> batch;
  batch.push_back(MakeSample(11, AstlValue{uint64_t{42}}, 123));

  REQUIRE(astl::ProtobufSerDes::SerializeCurrentBatch("serdes_on_disk_test_" + rand, batch, dir) ==
          ASTL_STATUS_SUCCESS);

  // Read file and ensure a single RawSampleBatch parses and matches
  std::ifstream ifs(file_path, std::ios::binary);
  REQUIRE(ifs.good());

  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(ifs);
  REQUIRE(out_or.has_value());
  REQUIRE(out_or->size() == 1);
  REQUIRE(out_or->at(0).operation_id == 11);
}
// NOLINTEND(readability-magic-numbers,readability-function-cognitive-complexity)

// NOLINTBEGIN(readability-magic-numbers)
TEST_CASE("RawSampleBatchReader reads each serialized batch separately") {
  const std::vector<RawSampledData> batch1{
      MakeSample(1, AstlValue{uint64_t{10}}, 100),
      MakeSample(1, AstlValue{uint64_t{20}}, 200),
  };
  const std::vector<RawSampledData> batch2{
      MakeSample(2, AstlValue{uint64_t{30}}, 300),
  };

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(batch1, str_stream) == ASTL_STATUS_SUCCESS);
  REQUIRE(astl::ProtobufSerDes::Serialize(batch2, str_stream) == ASTL_STATUS_SUCCESS);

  str_stream.seekg(0);
  astl::ProtobufSerDes::RawSampleBatchReader reader{str_stream};

  auto first_batch = reader.ReadNext();
  REQUIRE(first_batch.has_value());
  REQUIRE(first_batch->size() == 2);
  REQUIRE(first_batch->at(0).operation_id == 1);
  REQUIRE(first_batch->at(0).value == AstlValue{uint64_t{10}});
  REQUIRE(first_batch->at(1).value == AstlValue{uint64_t{20}});

  auto second_batch = reader.ReadNext();
  REQUIRE(second_batch.has_value());
  REQUIRE(second_batch->size() == 1);
  REQUIRE(second_batch->at(0).operation_id == 2);
  REQUIRE(second_batch->at(0).value == AstlValue{uint64_t{30}});

  auto eof = reader.ReadNext();
  REQUIRE(eof.has_value());
  REQUIRE(eof->empty());
}

TEST_CASE("RawSampleBatchReader returns parse errors") {
  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  str_stream << "not a protobuf";

  str_stream.seekg(0);
  astl::ProtobufSerDes::RawSampleBatchReader reader{str_stream};
  const auto                                 result = reader.ReadNext();

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_INTERNAL_ERROR);
}
// NOLINTEND(readability-magic-numbers)

// NOLINTBEGIN(readability-magic-numbers)
TEST_CASE("Deserialize errors when VALUE_NOT_SET") {
  // Craft a RawSampleBatch with one sample missing the oneof value.
  astl::protobuf::RawSampleBatch batch;
  auto*                          sample = batch.add_samples();
  sample->set_operation_id(1);
  sample->set_timestamp_us(999);  // no value set → VALUE_NOT_SET

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(google::protobuf::util::SerializeDelimitedToOstream(batch, &str_stream));

  str_stream.seekg(0);
  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(str_stream);
  REQUIRE_FALSE(out_or.has_value());
  REQUIRE(out_or.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}
// NOLINTEND(readability-magic-numbers)

// NOLINTBEGIN(readability-magic-numbers)
TEST_CASE("Deserialize errors on operation_id overflow") {
  // Build a protobuf batch directly with out-of-range operation_id.
  astl::protobuf::RawSampleBatch batch;
  auto*                          sample = batch.add_samples();
  sample->set_operation_id(std::numeric_limits<uint32_t>::max());  // likely > OperationId max
  sample->set_timestamp_us(1);
  auto* value = sample->mutable_value();
  value->set_uint32_value(7);

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(google::protobuf::util::SerializeDelimitedToOstream(batch, &str_stream));

  str_stream.seekg(0);
  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(str_stream);
  REQUIRE_FALSE(out_or.has_value());
  REQUIRE(out_or.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}
// NOLINTEND(readability-magic-numbers)

// NOLINTBEGIN(readability-magic-numbers)
TEST_CASE("Deserialize errors on invalid operation_id marker") {
  astl::protobuf::RawSampleBatch batch;
  auto*                          sample = batch.add_samples();
  sample->set_operation_id(astl::kOperationIdInvalid);
  sample->set_timestamp_us(1);
  auto* value = sample->mutable_value();
  value->set_uint32_value(7);

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(google::protobuf::util::SerializeDelimitedToOstream(batch, &str_stream));

  str_stream.seekg(0);
  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(str_stream);
  REQUIRE_FALSE(out_or.has_value());
  REQUIRE(out_or.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}
// NOLINTEND(readability-magic-numbers)

TEST_CASE("Deserialize fails with corrupt data") {
  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  str_stream << "not a protobuf";
  str_stream.seekg(0);

  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(str_stream);
  REQUIRE_FALSE(out_or.has_value());
  REQUIRE(out_or.error() == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("Serialize(ITopologyManager) + Deserialize<unique_ptr<ITopologyManager>> round-trip") {
  // Build a topology with two targets
  std::vector<std::unique_ptr<astl::ITarget>> vec;
  vec.push_back(std::make_unique<astl::ScmiTarget>("socket 1 telemetry", "Target discovered via SCMI", "tlm-1", nullptr,
                                                   std::string{"0xCAFEBABECAFEBABECAFEBABEBEEF0000"}));
  vec.push_back(std::make_unique<astl::ScmiTarget>("socket 0 telemetry", "Target discovered via SCMI", "tlm-0", nullptr,
                                                   std::string{"0xCAFEBABECAFEBABECAFEBABEBEEF0000"}));

  astl::TopologyManager topology_manager(std::move(vec));

  // Serialize to a stream
  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(topology_manager, str_stream) == ASTL_STATUS_SUCCESS);

  // Deserialize back
  str_stream.seekg(0);
  auto topology_manager_or_error =
      astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::ITopologyManager>>(str_stream);
  REQUIRE(topology_manager_or_error.has_value());
  auto rebuilt = std::move(*topology_manager_or_error);
  REQUIRE(rebuilt);

  // Verify topology contents
  const auto& targets = rebuilt->GetTargets();
  REQUIRE(targets.size() == 2);

  // Check first target properties
  {
    const auto& target_0 = *targets[0];
    REQUIRE(target_0.Name() == "socket 1 telemetry");
    REQUIRE(target_0.GetCollectorType() == astl::CollectorType::SCMI);
    REQUIRE(target_0.CollectorTargetPath().has_value());
    REQUIRE(*target_0.CollectorTargetPath() == "tlm-1");
    const auto* scmi_target_0 = dynamic_cast<const astl::ScmiTarget*>(&target_0);
    REQUIRE(scmi_target_0 != nullptr);
    REQUIRE(scmi_target_0->TelemetrySubdirectory() == "tlm-1");

    astl_target_props_t props{};
    REQUIRE(target_0.GetProperties(&props) == ASTL_STATUS_SUCCESS);
    REQUIRE(std::string{props.description ? props.description : ""} == "Target discovered via SCMI");
    REQUIRE(std::string{props.id ? props.id : ""} == "0xCAFEBABECAFEBABECAFEBABEBEEF0000");
  }

  // Check second target properties
  {
    const auto& target_1 = *targets[1];
    REQUIRE(target_1.Name() == "socket 0 telemetry");
    REQUIRE(target_1.GetCollectorType() == astl::CollectorType::SCMI);
    REQUIRE(target_1.CollectorTargetPath().has_value());
    REQUIRE(*target_1.CollectorTargetPath() == "tlm-0");
    const auto* scmi_target_1 = dynamic_cast<const astl::ScmiTarget*>(&target_1);
    REQUIRE(scmi_target_1 != nullptr);
    REQUIRE(scmi_target_1->TelemetrySubdirectory() == "tlm-0");

    astl_target_props_t props{};
    REQUIRE(target_1.GetProperties(&props) == ASTL_STATUS_SUCCESS);
    REQUIRE(std::string{props.description ? props.description : ""} == "Target discovered via SCMI");
    REQUIRE(std::string{props.id ? props.id : ""} == "0xCAFEBABECAFEBABECAFEBABEBEEF0000");
  }
}

TEST_CASE("Deserialize<unique_ptr<ITopologyManager>> rejects SCMI targets without scmi_sysfs_subdirectory") {
  astl::protobuf::TargetList proto_list;
  auto*                      proto_target = proto_list.add_targets();
  proto_target->set_name("socket 0 telemetry");
  proto_target->set_description("Target discovered via SCMI");
  proto_target->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));
  proto_target->set_id("0xCAFEBABECAFEBABECAFEBABEBEEF0000");

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(proto_list.SerializeToOstream(&str_stream));

  str_stream.seekg(0);
  auto topology_manager_or_error =
      astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::ITopologyManager>>(str_stream);
  REQUIRE_FALSE(topology_manager_or_error.has_value());
  REQUIRE(topology_manager_or_error.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}

TEST_CASE("MetricHandle + SampledValueMetric: protobuf round-trip", "[MetricHandle][SampledValueMetric][protobuf]") {
  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());

  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(
        MakeTarget("tlm-0", "unit-test target", astl::CollectorType::UNKNOWN, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
    REQUIRE(orch->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  }

  const auto& targets = orch->GetTargets();
  REQUIRE_FALSE(targets.empty());

  const astl::ITarget* tgt = targets[0].get();
  REQUIRE(tgt != nullptr);
  REQUIRE(tgt->Name() == "tlm-0");

  astl::MetricHandle handle;
  handle.config = std::make_unique<astl::MetricConfig>(
      "test_metric", "unit-test metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_VALUE, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});

  REQUIRE(handle.config);
  REQUIRE(handle.config->MetricType() == ASTL_METRIC_VALUE);
  REQUIRE(handle.config->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(handle.config->InputValueType() == ASTL_VALUE_UINT64);

  auto metric = std::make_unique<astl::SampledValueMetric>(handle.config.get(),  // const MetricConfig*
                                                           tgt,                  // const ITarget*
                                                           nullptr);             // IProcessedSampleSink*

  handle.target_to_metric_map.emplace(tgt, std::move(metric));
  REQUIRE(handle.target_to_metric_map.size() == 1);

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);

  cache_stream.seekg(0);
  auto metric_handles_or_err =
      astl::ProtobufSerDes::Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
  REQUIRE(metric_handles_or_err.has_value());

  auto rebuilt = std::move(*metric_handles_or_err);

  const auto& rebuilt_metric_handle = rebuilt.at(0);
  REQUIRE(rebuilt_metric_handle->config != nullptr);
  REQUIRE(rebuilt_metric_handle->config->Name() == "test_metric");
  REQUIRE(rebuilt_metric_handle->config->Description() == "unit-test metric");
  REQUIRE(rebuilt_metric_handle->config->MetricType() == ASTL_METRIC_VALUE);
  REQUIRE(rebuilt_metric_handle->config->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_metric_handle->config->InputValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_metric_handle->config->Units() == ASTL_UNITS_CELSIUS);

  REQUIRE(rebuilt_metric_handle->target_to_metric_map.size() == 1);

  auto it = rebuilt_metric_handle->target_to_metric_map.begin();
  REQUIRE(it != rebuilt_metric_handle->target_to_metric_map.end());

  const astl::ITarget*                  tgt_after   = it->first;
  const std::unique_ptr<astl::IMetric>& metric_uptr = it->second;
  auto*                                 sv_after    = dynamic_cast<astl::SampledValueMetric*>(metric_uptr.get());

  REQUIRE(tgt_after != nullptr);
  REQUIRE(metric_uptr != nullptr);
  REQUIRE(sv_after != nullptr);

  REQUIRE(sv_after->Summarize() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("MetricHandle + SampledValueMetric preserves input_value_type through protobuf",
          "[MetricHandle][SampledValueMetric][protobuf]") {
  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());
  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(
        MakeTarget("tlm-0", "unit-test target", astl::CollectorType::UNKNOWN, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
    REQUIRE(orch->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  }
  const auto& targets = orch->GetTargets();
  REQUIRE_FALSE(targets.empty());
  const astl::ITarget* target = targets[0].get();
  REQUIRE(target != nullptr);

  astl::MetricHandle handle;
  handle.config = std::make_unique<astl::MetricConfig>(
      "scaled_metric", "scaled metric", ASTL_UNITS_WATTS, ASTL_VALUE_FLOAT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_VALUE, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{}, astl::IdentityFormula{},
      ASTL_VALUE_UINT64);
  REQUIRE(handle.config != nullptr);
  REQUIRE(handle.config->ValueType() == ASTL_VALUE_FLOAT64);
  REQUIRE(handle.config->InputValueType() == ASTL_VALUE_UINT64);

  auto metric = std::make_unique<astl::SampledValueMetric>(handle.config.get(), target, nullptr);
  handle.target_to_metric_map.emplace(target, std::move(metric));

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);

  cache_stream.seekg(0);
  auto rebuilt_or_err =
      astl::ProtobufSerDes::Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
  REQUIRE(rebuilt_or_err.has_value());
  REQUIRE(rebuilt_or_err->size() == 1);
  const auto& rebuilt = rebuilt_or_err->at(0);
  REQUIRE(rebuilt != nullptr);
  REQUIRE(rebuilt->config != nullptr);
  REQUIRE(rebuilt->config->ValueType() == ASTL_VALUE_FLOAT64);
  REQUIRE(rebuilt->config->InputValueType() == ASTL_VALUE_UINT64);
}

TEST_CASE("MetricHandle + FiniteSetMetric: protobuf round-trip", "[MetricHandle][FiniteSetMetric][protobuf]") {
  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());

  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(
        MakeTarget("tlm-0", "unit-test target", astl::CollectorType::UNKNOWN, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
    REQUIRE(orch->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  }

  const auto& targets = orch->GetTargets();
  REQUIRE_FALSE(targets.empty());

  const astl::ITarget* tgt = targets[0].get();
  REQUIRE(tgt != nullptr);
  REQUIRE(tgt->Name() == "tlm-0");

  // Build a finite set + labels for a simple UINT64 finite set metric
  astl::FiniteSetMetric::FiniteSet finite_set = {
      astl::AstlValue{uint64_t{0}},
      astl::AstlValue{uint64_t{1}},
      astl::AstlValue{uint64_t{2}},
  };

  astl::FiniteSetMetricConfig::ValueToInfoMap state_info = {
      {astl::AstlValue{uint64_t{0}}, {"STATE_ZERO", "Value is zero"}},
      {astl::AstlValue{uint64_t{1}}, {"STATE_ONE", "Value is one"}  },
      {astl::AstlValue{uint64_t{2}}, {"STATE_TWO", "Value is two"}  },
  };

  astl::MetricHandle handle;
  handle.config = std::make_unique<astl::FiniteSetMetricConfig>(
      "finite_test_metric", "finite-set unit-test metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64,
      ASTL_METRIC_FINITE_SET_VALUE, ASTL_METRIC_IDENTIFIER_UNKNOWN, astl::CollectorType::UNKNOWN,
      astl::NullOperationBuilder{}, finite_set, state_info);

  REQUIRE(handle.config);
  REQUIRE(handle.config->MetricType() == ASTL_METRIC_FINITE_SET_VALUE);
  REQUIRE(handle.config->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(handle.config->InputValueType() == ASTL_VALUE_UINT64);

  auto* finite_cfg = dynamic_cast<astl::FiniteSetMetricConfig*>(handle.config.get());
  REQUIRE(finite_cfg != nullptr);

  auto metric = std::make_unique<astl::FiniteSetMetric>(finite_cfg,  // FiniteSetMetricConfig*
                                                        tgt,         // const ITarget*
                                                        nullptr);    // IProcessedSampleSink*

  handle.target_to_metric_map.emplace(tgt, std::move(metric));
  REQUIRE(handle.target_to_metric_map.size() == 1);

  // Serialize
  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);

  // Deserialize
  cache_stream.seekg(0);
  auto metric_handles_or_err =
      astl::ProtobufSerDes::Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
  REQUIRE(metric_handles_or_err.has_value());

  auto rebuilt = std::move(*metric_handles_or_err);
  REQUIRE(rebuilt.size() == 1);

  const auto& rebuilt_metric_handle = rebuilt.at(0);
  REQUIRE(rebuilt_metric_handle->config != nullptr);

  // Check config properties
  REQUIRE(rebuilt_metric_handle->config->Name() == "finite_test_metric");
  REQUIRE(rebuilt_metric_handle->config->Description() == "finite-set unit-test metric");
  REQUIRE(rebuilt_metric_handle->config->MetricType() == ASTL_METRIC_FINITE_SET_VALUE);
  REQUIRE(rebuilt_metric_handle->config->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_metric_handle->config->InputValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_metric_handle->config->Units() == ASTL_UNITS_NONE);

  auto* rebuilt_finite_cfg = dynamic_cast<astl::FiniteSetMetricConfig*>(rebuilt_metric_handle->config.get());
  REQUIRE(rebuilt_finite_cfg != nullptr);

  REQUIRE(rebuilt_metric_handle->target_to_metric_map.size() == 1);

  auto it = rebuilt_metric_handle->target_to_metric_map.begin();
  REQUIRE(it != rebuilt_metric_handle->target_to_metric_map.end());

  const astl::ITarget*                  tgt_after   = it->first;
  const std::unique_ptr<astl::IMetric>& metric_uptr = it->second;
  auto*                                 fs_after    = dynamic_cast<astl::FiniteSetMetric*>(metric_uptr.get());

  REQUIRE(tgt_after != nullptr);
  REQUIRE(metric_uptr != nullptr);
  REQUIRE(fs_after != nullptr);

  // The finite set should match what we configured (size-wise at least)
  const auto& finite_set_after = fs_after->GetFiniteSet();
  REQUIRE(finite_set_after.size() == 3);
  REQUIRE(finite_set_after.contains(astl::AstlValue{uint64_t{0}}));
  REQUIRE(finite_set_after.contains(astl::AstlValue{uint64_t{1}}));
  REQUIRE(finite_set_after.contains(astl::AstlValue{uint64_t{2}}));

  // State labels AND descriptions must survive the round-trip
  const auto& rebuilt_state_info = rebuilt_finite_cfg->GetStateInfo();
  REQUIRE(rebuilt_state_info.size() == 3);
  REQUIRE(rebuilt_state_info.at(astl::AstlValue{uint64_t{0}}).state_name == "STATE_ZERO");
  REQUIRE(rebuilt_state_info.at(astl::AstlValue{uint64_t{0}}).state_description == "Value is zero");
  REQUIRE(rebuilt_state_info.at(astl::AstlValue{uint64_t{1}}).state_name == "STATE_ONE");
  REQUIRE(rebuilt_state_info.at(astl::AstlValue{uint64_t{1}}).state_description == "Value is one");
  REQUIRE(rebuilt_state_info.at(astl::AstlValue{uint64_t{2}}).state_name == "STATE_TWO");
  REQUIRE(rebuilt_state_info.at(astl::AstlValue{uint64_t{2}}).state_description == "Value is two");

  // Summarize should still succeed
  REQUIRE(fs_after->Summarize() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Serialize(MetricHandle) covers null config and empty target map", "[MetricHandle][protobuf]") {
  {
    astl::MetricHandle handle;
    std::stringstream  cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(astl::ProtobufSerDes::Serialize(handle, cache_stream) == ASTL_STATUS_INTERNAL_ERROR);
  }

  {
    astl::MetricHandle handle;
    handle.config = std::make_unique<astl::MetricConfig>("empty_metric", "empty", ASTL_UNITS_NONE, ASTL_VALUE_UINT64,
                                                         ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
                                                         astl::CollectorType::SCMI, astl::NullOperationBuilder{});
    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(astl::ProtobufSerDes::Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);
  }
}

TEST_CASE("Serialize(IMetricManager) rejects non-concrete metric manager", "[MetricManager][protobuf]") {
  MockMetricManager mock_metric_manager;
  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);

  REQUIRE(astl::ProtobufSerDes::Serialize(static_cast<const astl::IMetricManager&>(mock_metric_manager),
                                          cache_stream) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("Serialize(IMetricManager) round-trip through MetricManager", "[MetricManager][protobuf]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(
      MakeTarget("tlm-0", "unit-test target", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
  const astl::ITarget* tgt = targets[0].get();
  REQUIRE(tgt != nullptr);

  auto cfg = std::make_unique<astl::MetricConfig>(
      "test_metric", "unit-test metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_VALUE, astl::CollectorType::SCMI, astl::ScmiOperationBuilder{astl::ScmiDataEventId{0x24}});
  REQUIRE(cfg != nullptr);

  astl::MetricManager metric_manager{MakeCaps(astl::CollectorType::SCMI)};
  REQUIRE(metric_manager.RegisterMetric(std::move(cfg), {tgt}) == ASTL_STATUS_SUCCESS);

  auto metrics_or_err = metric_manager.GetAvailableMetrics(tgt);
  REQUIRE(metrics_or_err.has_value());
  REQUIRE(metrics_or_err->size() == 1);
  const astl_metric_handle_t metric_handle = metrics_or_err->front();

  auto operations_or_err = metric_manager.GetRequiredOperations({&metric_handle, 1}, tgt);
  REQUIRE(operations_or_err.has_value());
  REQUIRE(operations_or_err->operationsOnSample.size() == 1);
  const auto operation_id = operations_or_err->operationsOnSample.front()->GetId();

  // Act: serialize via the IMetricManager overload (dynamic_cast inside)
  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  const auto        status =
      astl::ProtobufSerDes::Serialize(static_cast<const astl::IMetricManager&>(metric_manager), cache_stream);

  // Assert
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(cache_stream.tellp() > 0);

  cache_stream.seekg(0);
  auto rebuilt_or_err = astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::IMetricManager>>(cache_stream, targets);
  REQUIRE(rebuilt_or_err.has_value());

  auto rebuilt_mgr = std::move(rebuilt_or_err.value());
  REQUIRE(rebuilt_mgr != nullptr);

  // Basic sanity: metric still available on the same target
  auto rebuilt_metrics_or_err = rebuilt_mgr->GetAvailableMetrics(tgt);
  REQUIRE(rebuilt_metrics_or_err.has_value());
  auto handles = rebuilt_metrics_or_err.value();
  REQUIRE(handles.size() == 1);

  const auto* rebuilt_handle = static_cast<const astl::MetricHandle*>(rebuilt_metrics_or_err.value()[0]);
  REQUIRE(rebuilt_handle != nullptr);
  REQUIRE(rebuilt_handle->config != nullptr);

  REQUIRE(rebuilt_handle->config->Name() == "test_metric");
  REQUIRE(rebuilt_handle->config->Description() == "unit-test metric");
  REQUIRE(rebuilt_handle->config->MetricType() == ASTL_METRIC_VALUE);
  REQUIRE(rebuilt_handle->config->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_handle->config->InputValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_handle->config->Units() == ASTL_UNITS_CELSIUS);

  REQUIRE(rebuilt_handle->target_to_metric_map.size() == 1);
  auto rebuilt_it = rebuilt_handle->target_to_metric_map.find(tgt);
  REQUIRE(rebuilt_it != rebuilt_handle->target_to_metric_map.end());
  REQUIRE(rebuilt_it->second != nullptr);

  astl::ClockCorrelationMap corr;
  corr[operation_id] = astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{0}},
                                                       uint64_t{0}, astl::MakeTickRatio<astl::SampleMicroseconds>()};
  rebuilt_mgr->SetClockCorrelations(corr);

  astl::RawSamplesMap samples_map;
  samples_map[tgt] = {MakeSample(operation_id, AstlValue{uint64_t{99}}, 1000)};  // NOLINT
  REQUIRE(rebuilt_mgr->ProcessRawSamples(samples_map) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Serialize(IMetricManager) round-trips procfs composite metrics", "[MetricManager][protobuf]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(MakeTarget("procfs", "unit-test procfs target", astl::CollectorType::PROCFS));
  const astl::ITarget* target = targets.front().get();
  REQUIRE(target != nullptr);

  std::vector<astl::ProcfsCompositeMetricConfig::InputBinding> inputs;
  inputs.push_back({
      "total", astl::procfs::KeyValueField{"meminfo", "MemTotal", ASTL_VALUE_UINT64}
  });
  inputs.push_back({
      "available", astl::procfs::KeyValueField{"meminfo", "MemAvailable", ASTL_VALUE_UINT64}
  });
  auto config = std::make_unique<astl::ProcfsCompositeMetricConfig>(astl::ProcfsCompositeMetricConfig::CreateParams{
      .name          = "memory-used",
      .description   = "Memory used",
      .units         = ASTL_UNITS_BYTES,
      .value_type    = ASTL_VALUE_UINT64,
      .identifier    = ASTL_METRIC_IDENTIFIER_UNKNOWN,
      .metric_type   = ASTL_METRIC_VALUE,
      .inputs        = std::move(inputs),
      .formula_text  = "max(total - available, 0)",
      .metric_groups = {},
      .metric_id     = "procfs::memory-used",
  });

  astl::MetricManager metric_manager{MakeCaps(astl::CollectorType::PROCFS)};
  REQUIRE(metric_manager.RegisterMetric(std::move(config), {target}) == ASTL_STATUS_SUCCESS);
  auto metrics = metric_manager.GetAvailableMetrics(target);
  REQUIRE(metrics.has_value());
  REQUIRE(metrics->size() == 1);

  const auto* const metric_handle = metrics->front();
  auto              operations    = metric_manager.GetRequiredOperations({&metric_handle, 1}, target);
  REQUIRE(operations.has_value());
  REQUIRE(operations->operationsOnSample.size() == 2);
  const auto first_operation_id  = operations->operationsOnSample[0]->GetId();
  const auto second_operation_id = operations->operationsOnSample[1]->GetId();

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(Serialize(static_cast<const astl::IMetricManager&>(metric_manager), cache_stream) == ASTL_STATUS_SUCCESS);
  cache_stream.seekg(0);

  auto rebuilt = Deserialize<std::unique_ptr<astl::IMetricManager>>(cache_stream, targets);
  REQUIRE(rebuilt.has_value());
  auto rebuilt_manager = std::move(*rebuilt);

  auto rebuilt_metrics = rebuilt_manager->GetAvailableMetrics(target);
  REQUIRE(rebuilt_metrics.has_value());
  REQUIRE(rebuilt_metrics->size() == 1);
  const auto* rebuilt_handle = static_cast<const astl::MetricHandle*>(rebuilt_metrics->front());
  REQUIRE(dynamic_cast<const astl::ProcfsCompositeMetricConfig*>(rebuilt_handle->config.get()) != nullptr);

  astl::ClockCorrelationMap correlations;
  correlations[first_operation_id] =
      astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{0}}, uint64_t{0},
                                      astl::MakeTickRatio<astl::SampleMicroseconds>()};
  correlations[second_operation_id] = correlations[first_operation_id];
  rebuilt_manager->SetClockCorrelations(correlations);

  astl::RawSamplesMap samples;
  samples[target] = {
      MakeSample(first_operation_id, AstlValue{uint64_t{100}}, 1000),
      MakeSample(second_operation_id, AstlValue{uint64_t{25}}, 1000),
  };
  REQUIRE(rebuilt_manager->ProcessRawSamples(samples) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Serialize(IMetricManager) round-trips counters through MetricManager", "[MetricManager][protobuf]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(
      MakeTarget("tlm-0", "unit-test target", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
  const astl::ITarget* target = targets[0].get();
  REQUIRE(target != nullptr);

  astl::MetricManager metric_manager{MakeCaps(astl::CollectorType::SCMI)};
  auto                counter_config = std::make_unique<astl::MetricConfig>(
      "raw_counter", "unit-test counter", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_VALUE, astl::CollectorType::SCMI, astl::ScmiOperationBuilder{astl::ScmiDataEventId{0x42}},
      astl::IdentityFormula{}, ASTL_VALUE_UINT64, std::vector<std::string>{}, "counter::raw_counter");

  REQUIRE(metric_manager.RegisterCounter(std::move(counter_config), {target}) == ASTL_STATUS_SUCCESS);

  auto counters_or_err = metric_manager.GetAvailableCounters(target);
  REQUIRE(counters_or_err.has_value());
  REQUIRE(counters_or_err->size() == 1);

  const astl_counter_handle_t counter_handle = counters_or_err->front();
  auto operations_or_err = metric_manager.GetCounterRequiredOperations({&counter_handle, 1}, target);
  REQUIRE(operations_or_err.has_value());
  REQUIRE(operations_or_err->operationsOnSample.size() == 1);
  const auto operation_id = operations_or_err->operationsOnSample.front()->GetId();

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(static_cast<const astl::IMetricManager&>(metric_manager), cache_stream) ==
          ASTL_STATUS_SUCCESS);

  cache_stream.seekg(0);
  auto rebuilt_or_err = astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::IMetricManager>>(cache_stream, targets);
  REQUIRE(rebuilt_or_err.has_value());
  auto rebuilt_mgr = std::move(rebuilt_or_err.value());
  REQUIRE(rebuilt_mgr != nullptr);

  auto rebuilt_counters_or_err = rebuilt_mgr->GetAvailableCounters(target);
  REQUIRE(rebuilt_counters_or_err.has_value());
  REQUIRE(rebuilt_counters_or_err->size() == 1);

  astl_counter_props_t props{};
  REQUIRE(rebuilt_mgr->GetCounterProperties(rebuilt_counters_or_err->front(), &props) == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string{props.name} == "raw_counter");
  REQUIRE(std::string{props.description} == "unit-test counter");
  REQUIRE(props.value_type == ASTL_VALUE_UINT64);

  astl::ClockCorrelationMap corr;
  corr[operation_id] = astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{0}},
                                                       uint64_t{0}, astl::MakeTickRatio<astl::SampleMicroseconds>()};
  rebuilt_mgr->SetClockCorrelations(corr);

  astl::RawSamplesMap samples_map;
  samples_map[target] = {MakeSample(operation_id, AstlValue{uint64_t{99}}, 1000)};  // NOLINT
  REQUIRE(rebuilt_mgr->ProcessRawSamples(samples_map) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Deserialize<MetricManager> rejects malformed counter payloads", "[MetricManager][protobuf]") {
  InstallSingleScmiTargetTlm0();
  const auto& orch    = astl::Orchestrator::GetInstance()->get();
  const auto& targets = orch->GetTargets();

  auto deserialize = [&targets](astl::protobuf::MetricManager& proto_mgr) {
    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_mgr.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);
    return astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);
  };

  SECTION("missing target ids") {
    auto  proto_mgr = BuildValidMetricManagerProto();
    auto* raw       = AddCounterToMetricManagerProto(proto_mgr);
    raw->clear_target_ids();

    auto mgr_or_err = deserialize(proto_mgr);
    REQUIRE_FALSE(mgr_or_err.has_value());
    REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("duplicate target ids") {
    auto  proto_mgr = BuildValidMetricManagerProto();
    auto* raw       = AddCounterToMetricManagerProto(proto_mgr);
    raw->add_target_ids("tlm-0");

    auto mgr_or_err = deserialize(proto_mgr);
    REQUIRE_FALSE(mgr_or_err.has_value());
    REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("unknown target id") {
    auto  proto_mgr = BuildValidMetricManagerProto();
    auto* raw       = AddCounterToMetricManagerProto(proto_mgr);
    raw->set_target_ids(0, "missing-target");

    auto mgr_or_err = deserialize(proto_mgr);
    REQUIRE_FALSE(mgr_or_err.has_value());
    REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("duplicate counter ids") {
    auto proto_mgr = BuildValidMetricManagerProto();
    AddCounterToMetricManagerProto(proto_mgr, "duplicate_counter");
    AddCounterToMetricManagerProto(proto_mgr, "duplicate_counter");

    auto mgr_or_err = deserialize(proto_mgr);
    REQUIRE_FALSE(mgr_or_err.has_value());
    REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

TEST_CASE("Deserialize<vector<MetricHandle>> rejects malformed metric payloads", "[MetricHandle][protobuf]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(
      MakeTarget("tlm-0", "unit-test target", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));

  SECTION("missing target ids") {
    astl::protobuf::RawMetricVec proto_metrics;
    auto*                        raw = proto_metrics.add_metrics();
    raw->set_metric_id("test_metric");

    auto* cfg = raw->mutable_config();
    cfg->set_metric_name("test_metric");
    cfg->set_description("unit-test metric");
    cfg->set_units(static_cast<astl::protobuf::AstlUnits>(ASTL_UNITS_CELSIUS));
    cfg->set_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
    cfg->set_input_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
    cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_VALUE));
    cfg->set_identifier(static_cast<astl::protobuf::AstlMetricIdentifier>(ASTL_METRIC_IDENTIFIER_UNKNOWN));
    cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_metrics.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);

    auto handles_or_err =
        astl::ProtobufSerDes::Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
    REQUIRE_FALSE(handles_or_err.has_value());
    REQUIRE(handles_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("unknown target id") {
    astl::protobuf::RawMetricVec proto_metrics;
    auto*                        raw = proto_metrics.add_metrics();
    raw->set_metric_id("test_metric");
    raw->add_target_ids("missing-target");

    auto* cfg = raw->mutable_config();
    cfg->set_metric_name("test_metric");
    cfg->set_description("unit-test metric");
    cfg->set_units(static_cast<astl::protobuf::AstlUnits>(ASTL_UNITS_CELSIUS));
    cfg->set_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
    cfg->set_input_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
    cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_VALUE));
    cfg->set_identifier(static_cast<astl::protobuf::AstlMetricIdentifier>(ASTL_METRIC_IDENTIFIER_UNKNOWN));
    cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_metrics.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);

    auto handles_or_err =
        astl::ProtobufSerDes::Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
    REQUIRE_FALSE(handles_or_err.has_value());
    REQUIRE(handles_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("finite set metric missing finite set payload") {
    astl::protobuf::RawMetricVec proto_metrics;
    auto*                        raw = proto_metrics.add_metrics();
    raw->set_metric_id("finite_metric");
    raw->add_target_ids("tlm-0");

    auto* cfg = raw->mutable_config();
    cfg->set_metric_name("finite_metric");
    cfg->set_description("unit-test finite metric");
    cfg->set_units(static_cast<astl::protobuf::AstlUnits>(ASTL_UNITS_NONE));
    cfg->set_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
    cfg->set_input_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
    cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_FINITE_SET_VALUE));
    cfg->set_identifier(static_cast<astl::protobuf::AstlMetricIdentifier>(ASTL_METRIC_IDENTIFIER_UNKNOWN));
    cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_metrics.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);

    auto handles_or_err =
        astl::ProtobufSerDes::Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
    REQUIRE_FALSE(handles_or_err.has_value());
    REQUIRE(handles_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

TEST_CASE("Deserialize<MetricManager> fails on invalid protobuf input", "[MetricManager][protobuf]") {
  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  cache_stream << "this is not a valid MetricManager protobuf";

  cache_stream.seekg(0);
  auto mgr_or_err = astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, {});

  REQUIRE_FALSE(mgr_or_err.has_value());
  REQUIRE(mgr_or_err.error() == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE(
    "Deserialize<MetricManager> rebuilds capabilities, metrics "
    "and operation map",
    "[MetricManager][protobuf]") {
  // Arrange: Orchestrator with a target that matches the proto's target_id
  InstallSingleScmiTargetTlm0();
  const auto& orch    = astl::Orchestrator::GetInstance()->get();
  const auto& targets = orch->GetTargets();

  auto proto_mgr = BuildValidMetricManagerProto();

  // Add one operation->(metric,target) mapping
  auto* op_entry = proto_mgr.add_operation_to_metric_map();
  op_entry->set_operation_id(42U);  // NOLINT
  op_entry->set_metric_id("test_metric");
  op_entry->set_target_id("tlm-0");

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(proto_mgr.SerializeToOstream(&cache_stream));
  cache_stream.seekg(0);

  // Act
  auto mgr_or_err = Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);

  // Assert: deserialization succeeds and at least one metric handle exists
  REQUIRE(mgr_or_err.has_value());
  auto mgr = std::move(mgr_or_err.value());
  REQUIRE(mgr != nullptr);

  // Use the rebuilt routing map to prove the operation map is target-scoped and functional.
  auto metrics_or_err = mgr->GetAvailableMetrics(targets[0].get());
  REQUIRE(metrics_or_err.has_value());
  REQUIRE_FALSE(metrics_or_err->empty());
  const auto* first_handle = static_cast<const astl::MetricHandle*>((*metrics_or_err)[0]);
  REQUIRE(first_handle != nullptr);
  REQUIRE(first_handle->config != nullptr);
  REQUIRE(first_handle->config->InputValueType() == first_handle->config->ValueType());

  astl::ClockCorrelationMap corr;
  corr[42U] = astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{0}}, uint64_t{0},
                                              astl::MakeTickRatio<astl::SampleMicroseconds>()};
  mgr->SetClockCorrelations(corr);

  astl::RawSamplesMap samples_map;
  samples_map[targets[0].get()] = {MakeSample(42U, AstlValue{uint64_t{77}}, 1000)};  // NOLINT
  REQUIRE(mgr->ProcessRawSamples(samples_map) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Deserialize<MetricManager> rejects metric groups without top-level metadata", "[MetricManager][protobuf]") {
  InstallSingleScmiTargetTlm0();
  const auto& orch    = astl::Orchestrator::GetInstance()->get();
  const auto& targets = orch->GetTargets();

  auto proto_mgr = BuildValidMetricManagerProto();
  proto_mgr.mutable_metrics()->mutable_metrics(0)->mutable_config()->add_metric_groups("thermal");

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(proto_mgr.SerializeToOstream(&cache_stream));
  cache_stream.seekg(0);

  auto mgr_or_err = Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);
  REQUIRE_FALSE(mgr_or_err.has_value());
  REQUIRE(mgr_or_err.error() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("Deserialize<MetricManager> rejects malformed procfs composite metrics", "[MetricManager][protobuf]") {
  InstallSingleScmiTargetTlm0();
  const auto& targets = astl::Orchestrator::GetInstance()->get()->GetTargets();

  const auto build_composite_proto = []() {
    auto  proto_manager = BuildValidMetricManagerProto();
    auto* config        = proto_manager.mutable_metrics()->mutable_metrics(0)->mutable_config();
    config->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::PROCFS));
    auto* composite = config->mutable_procfs_composite();
    composite->add_input_names("first");
    composite->add_input_names("second");
    composite->set_formula("first + second");
    return proto_manager;
  };

  const auto deserialize = [&targets](astl::protobuf::MetricManager& proto_manager) {
    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_manager.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);
    return Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);
  };

  SECTION("duplicate input names") {
    auto  proto_manager = build_composite_proto();
    auto* composite = proto_manager.mutable_metrics()->mutable_metrics(0)->mutable_config()->mutable_procfs_composite();
    composite->set_input_names(1, "first");
    for (uint32_t operation_id : {41U, 42U}) {
      auto* operation = proto_manager.add_operation_to_metric_map();
      operation->set_operation_id(operation_id);
      operation->set_metric_id("test_metric");
      operation->set_target_id("tlm-0");
    }

    auto result = deserialize(proto_manager);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

TEST_CASE("Deserialize<MetricManager> rejects invalid operation map references", "[MetricManager][protobuf]") {
  InstallSingleScmiTargetTlm0();
  const auto& orch    = astl::Orchestrator::GetInstance()->get();
  const auto& targets = orch->GetTargets();

  SECTION("unknown metric id") {
    auto proto_mgr = BuildValidMetricManagerProto();

    auto* op_entry = proto_mgr.add_operation_to_metric_map();
    op_entry->set_operation_id(42U);
    op_entry->set_metric_id("missing_metric");
    op_entry->set_target_id("tlm-0");

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_mgr.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);

    auto mgr_or_err = Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);
    REQUIRE_FALSE(mgr_or_err.has_value());
    REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("unknown target id for valid metric") {
    auto proto_mgr = BuildValidMetricManagerProto();

    auto* op_entry = proto_mgr.add_operation_to_metric_map();
    op_entry->set_operation_id(42U);
    op_entry->set_metric_id("test_metric");
    op_entry->set_target_id("missing-target");

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(proto_mgr.SerializeToOstream(&cache_stream));
    cache_stream.seekg(0);

    auto mgr_or_err = Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);
    REQUIRE_FALSE(mgr_or_err.has_value());
    REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

TEST_CASE("Deserialize<MetricManager> rejects duplicate metric ids", "[MetricManager][protobuf]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(
      MakeTarget("tlm-0", "unit-test target", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));

  auto  proto_mgr        = BuildValidMetricManagerProto();
  auto* duplicate_metric = proto_mgr.mutable_metrics()->add_metrics();
  duplicate_metric->set_metric_id("test_metric");
  duplicate_metric->add_target_ids("tlm-0");

  auto* cfg = duplicate_metric->mutable_config();
  cfg->set_metric_name("duplicate_test_metric");
  cfg->set_description("duplicate unit-test metric");
  cfg->set_units(static_cast<astl::protobuf::AstlUnits>(ASTL_UNITS_CELSIUS));
  cfg->set_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
  cfg->set_input_value_type(static_cast<astl::protobuf::AstlValueType>(ASTL_VALUE_UINT64));
  cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_VALUE));
  cfg->set_identifier(static_cast<astl::protobuf::AstlMetricIdentifier>(ASTL_METRIC_IDENTIFIER_UNKNOWN));
  cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(proto_mgr.SerializeToOstream(&cache_stream));
  cache_stream.seekg(0);

  auto mgr_or_err = Deserialize<std::unique_ptr<astl::MetricManager>>(cache_stream, targets);
  REQUIRE_FALSE(mgr_or_err.has_value());
  REQUIRE(mgr_or_err.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}

TEST_CASE("MetricHandle protobuf round-trips grouped metrics across supported concrete types",
          "[MetricHandle][protobuf][roundtrip]") {
  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());

  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(
        MakeTarget("tlm-0", "unit-test target 0", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
    targets.push_back(
        MakeTarget("tlm-1", "unit-test target 1", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0001"));
    REQUIRE(orch->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  }

  const auto& targets = orch->GetTargets();
  REQUIRE(targets.size() == 2);
  const astl::ITarget* target0 = targets[0].get();
  const astl::ITarget* target1 = targets[1].get();
  REQUIRE(target0 != nullptr);
  REQUIRE(target1 != nullptr);

  SECTION("event metric preserves groups and both targets") {
    astl::MetricHandle handle;
    handle.config = std::make_unique<astl::MetricConfig>(
        "event_metric", "event metric with groups", ASTL_UNITS_NONE, ASTL_VALUE_UNKNOWN, ASTL_METRIC_IDENTIFIER_UNKNOWN,
        ASTL_METRIC_EVENT, astl::CollectorType::SCMI, astl::NullOperationBuilder{}, astl::IdentityFormula{},
        ASTL_VALUE_UNKNOWN, std::vector<std::string>{"thermal", "lifecycle"});
    auto* cfg = handle.config.get();

    handle.target_to_metric_map.emplace(target0, std::make_unique<astl::EventMetric>(cfg, target0, nullptr));
    handle.target_to_metric_map.emplace(target1, std::make_unique<astl::EventMetric>(cfg, target1, nullptr));

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);

    cache_stream.seekg(0);
    auto rebuilt_or_err = Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
    REQUIRE(rebuilt_or_err.has_value());
    REQUIRE(rebuilt_or_err->size() == 1);

    const auto& rebuilt = rebuilt_or_err->at(0);
    REQUIRE(rebuilt->config != nullptr);
    REQUIRE(rebuilt->config->MetricType() == ASTL_METRIC_EVENT);
    REQUIRE(rebuilt->config->MetricGroups() == std::vector<std::string>{"thermal", "lifecycle"});
    REQUIRE(rebuilt->target_to_metric_map.size() == 2);
    REQUIRE(dynamic_cast<astl::EventMetric*>(rebuilt->target_to_metric_map.at(target0).get()) != nullptr);
    REQUIRE(dynamic_cast<astl::EventMetric*>(rebuilt->target_to_metric_map.at(target1).get()) != nullptr);
  }

  SECTION("delta metric round-trips") {
    astl::MetricHandle handle;
    handle.config = std::make_unique<astl::MetricConfig>(
        "delta_metric", "delta metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
        ASTL_METRIC_DELTA, astl::CollectorType::SCMI, astl::NullOperationBuilder{});
    auto* cfg = handle.config.get();

    handle.target_to_metric_map.emplace(target0, std::make_unique<astl::DeltaMetric>(cfg, target0, nullptr));
    handle.target_to_metric_map.emplace(target1, std::make_unique<astl::DeltaMetric>(cfg, target1, nullptr));

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);

    cache_stream.seekg(0);
    auto rebuilt_or_err = Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
    REQUIRE(rebuilt_or_err.has_value());
    REQUIRE(rebuilt_or_err->size() == 1);
    REQUIRE(rebuilt_or_err->at(0)->config->MetricType() == ASTL_METRIC_DELTA);
    REQUIRE(rebuilt_or_err->at(0)->target_to_metric_map.size() == 2);
    REQUIRE(dynamic_cast<astl::DeltaMetric*>(rebuilt_or_err->at(0)->target_to_metric_map.at(target0).get()) != nullptr);
    REQUIRE(dynamic_cast<astl::DeltaMetric*>(rebuilt_or_err->at(0)->target_to_metric_map.at(target1).get()) != nullptr);
  }

  SECTION("rate metric round-trips") {
    astl::MetricHandle handle;
    handle.config = std::make_unique<astl::MetricConfig>(
        "rate_metric", "rate metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
        ASTL_METRIC_RATE, astl::CollectorType::SCMI, astl::NullOperationBuilder{});
    auto* cfg = handle.config.get();

    handle.target_to_metric_map.emplace(target0, std::make_unique<astl::RateMetric>(cfg, target0, nullptr));
    handle.target_to_metric_map.emplace(target1, std::make_unique<astl::RateMetric>(cfg, target1, nullptr));

    std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(Serialize(handle, cache_stream) == ASTL_STATUS_SUCCESS);

    cache_stream.seekg(0);
    auto rebuilt_or_err = Deserialize<std::vector<std::unique_ptr<astl::MetricHandle>>>(cache_stream, targets);
    REQUIRE(rebuilt_or_err.has_value());
    REQUIRE(rebuilt_or_err->size() == 1);
    REQUIRE(rebuilt_or_err->at(0)->config->MetricType() == ASTL_METRIC_RATE);
    REQUIRE(rebuilt_or_err->at(0)->target_to_metric_map.size() == 2);
    REQUIRE(dynamic_cast<astl::RateMetric*>(rebuilt_or_err->at(0)->target_to_metric_map.at(target0).get()) != nullptr);
    REQUIRE(dynamic_cast<astl::RateMetric*>(rebuilt_or_err->at(0)->target_to_metric_map.at(target1).get()) != nullptr);
  }
}

TEST_CASE("Serialize(MetricHandle) rejects unsupported metric types", "[MetricHandle][protobuf]") {
  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());
  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  {
    std::vector<std::unique_ptr<astl::ITarget>> targets;
    targets.push_back(
        MakeTarget("tlm-0", "unit-test target", astl::CollectorType::SCMI, "0xCAFEBABECAFEBABECAFEBABEBEEF0000"));
    REQUIRE(orch->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);
  }

  const astl::ITarget* target = orch->GetTargets()[0].get();
  REQUIRE(target != nullptr);

  astl::MetricHandle handle;
  handle.config = std::make_unique<astl::MetricConfig>(
      "unknown_metric", "unsupported metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN,
      ASTL_METRIC_UNKNOWN, astl::CollectorType::SCMI, astl::NullOperationBuilder{});
  handle.target_to_metric_map.emplace(target,
                                      std::make_unique<astl::SampledValueMetric>(handle.config.get(), target, nullptr));

  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(Serialize(handle, cache_stream) == ASTL_STATUS_INTERNAL_ERROR);
}
