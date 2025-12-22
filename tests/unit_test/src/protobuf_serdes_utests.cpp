#include <google/protobuf/stubs/common.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>

#include "../../mock_classes.hpp"
#include "../../test_utilities.hpp"
#include "astl/astl_errors.h"
#include "capabilities.hpp"
#include "metric/finite_set_metric.hpp"
#include "metric/metric_manager.hpp"
#include "metric/sampled_value_metric.hpp"
#include "orchestrator/orchestrator.hpp"
#include "serdes/metrics.pb.h"
#include "serdes/protobuf_serdes.hpp"
#include "serdes/raw_samples.pb.h"  // AUTO-GENERATED RawSampleBatch
#include "serdes/targets.pb.h"      // AUTO-GENERATED Target
#include "topology/i_topology_manager.hpp"
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
  sample.timestamp = SampleTimestamp{SampleTimestamp::duration{std::chrono::microseconds{ts_us}}};
  return sample;
}

auto MakeTarget(std::string name, std::string description, astl::CollectorType collector_type,
                std::optional<std::string> uuid = std::nullopt) -> std::unique_ptr<astl::Target> {
  return std::make_unique<astl::Target>(std::move(name), std::move(description), collector_type, nullptr,
                                        std::move(uuid));
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
  cfg->set_metric_type(static_cast<astl::protobuf::AstlMetricType>(ASTL_METRIC_VALUE));
  cfg->set_category(static_cast<astl::protobuf::AstlCategory>(ASTL_CATEGORY_UNCATEGORIZED));
  cfg->set_collector_type(static_cast<astl::protobuf::CollectorType>(astl::CollectorType::SCMI));

  return proto_mgr;
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
  input.push_back(MakeSample(8, AstlValue{std::string{"hi"}}, 80));

  std::stringstream str_stream(std::ios::in | std::ios::out | std::ios::binary);
  REQUIRE(astl::ProtobufSerDes::Serialize(input, str_stream) == ASTL_STATUS_SUCCESS);

  str_stream.seekg(0);
  auto out_or = astl::ProtobufSerDes::Deserialize<std::vector<RawSampledData>>(str_stream);
  REQUIRE(out_or.has_value());
  const auto& out = *out_or;

  REQUIRE(out.size() == input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    REQUIRE(out[i].operation_id == input[i].operation_id);
    REQUIRE(out[i].timestamp.time_since_epoch() == input[i].timestamp.time_since_epoch());
    REQUIRE(out[i].value == input[i].value);
  }
}
// NOLINTEND(readability-magic-numbers,readability-function-cognitive-complexity)

TEST_CASE("SerializeCurrentBatch returns NO_DATA_COLLECTED on empty input") {
  std::vector<RawSampledData> empty;
  REQUIRE(astl::ProtobufSerDes::SerializeCurrentBatch("serdes_empty_test", empty) == ASTL_STATUS_NO_DATA_COLLECTED);
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
  TempFileGuard                           guard{file_path};

  // Clean slate
  std::error_code err_code;
  fs::create_directories(file_path.parent_path(), err_code);
  fs::remove(file_path, err_code);

  std::vector<RawSampledData> batch;
  batch.push_back(MakeSample(11, AstlValue{uint64_t{42}}, 123));

  REQUIRE(astl::ProtobufSerDes::SerializeCurrentBatch("serdes_on_disk_test_" + rand, batch) == ASTL_STATUS_SUCCESS);

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
  vec.push_back(MakeTarget("tlm-1", "Target discovered via SCMI", astl::CollectorType::SCMI,
                           std::string{"0xCAFEBABECAFEBABECAFEBABEBEEF0000"}));
  vec.push_back(MakeTarget("tlm-0", "Target discovered via SCMI", astl::CollectorType::SCMI,
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
    REQUIRE(target_0.Name() == "tlm-1");
    REQUIRE(target_0.GetCollectorType() == astl::CollectorType::SCMI);

    astl_target_properties_t props{};
    REQUIRE(target_0.GetProperties(&props) == ASTL_STATUS_SUCCESS);
    REQUIRE(std::string{props._description ? props._description : ""} == "Target discovered via SCMI");
    REQUIRE(std::string{props._uuid ? props._uuid : ""} == "0xCAFEBABECAFEBABECAFEBABEBEEF0000");
  }

  // Check second target properties
  {
    const auto& target_1 = *targets[1];
    REQUIRE(target_1.Name() == "tlm-0");
    REQUIRE(target_1.GetCollectorType() == astl::CollectorType::SCMI);

    astl_target_properties_t props{};
    REQUIRE(target_1.GetProperties(&props) == ASTL_STATUS_SUCCESS);
    REQUIRE(std::string{props._description ? props._description : ""} == "Target discovered via SCMI");
    REQUIRE(std::string{props._uuid ? props._uuid : ""} == "0xCAFEBABECAFEBABECAFEBABEBEEF0000");
  }
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
      "test_metric", "unit-test metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
      ASTL_METRIC_VALUE, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});

  REQUIRE(handle.config);
  REQUIRE(handle.config->MetricType() == ASTL_METRIC_VALUE);
  REQUIRE(handle.config->ValueType() == ASTL_VALUE_UINT64);

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

  astl::FiniteSetMetric::ValueToLabel labels = {
      {astl::AstlValue{uint64_t{0}}, "STATE_ZERO"},
      {astl::AstlValue{uint64_t{1}}, "STATE_ONE" },
      {astl::AstlValue{uint64_t{2}}, "STATE_TWO" },
  };

  astl::MetricHandle handle;
  handle.config = std::make_unique<astl::FiniteSetMetricConfig>(
      "finite_test_metric", "finite-set unit-test metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64,
      ASTL_METRIC_FINITE_SET_VALUE, ASTL_CATEGORY_UNCATEGORIZED, astl::CollectorType::UNKNOWN,
      astl::NullOperationBuilder{}, finite_set, labels);

  REQUIRE(handle.config);
  REQUIRE(handle.config->MetricType() == ASTL_METRIC_FINITE_SET_VALUE);
  REQUIRE(handle.config->ValueType() == ASTL_VALUE_UINT64);

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

  // Summarize should still succeed
  REQUIRE(fs_after->Summarize() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Serialize(IMetricManager) round-trip through MetricManager", "[MetricManager][protobuf]") {
  // Arrange: build a real MetricManager via Orchestrator and inject one metric
  const astl::ITarget* tgt = InstallSingleScmiTargetTlm0();

  auto orch_expected = astl::Orchestrator::GetInstance();
  REQUIRE(orch_expected.has_value());
  auto* orch = orch_expected->get().get();
  REQUIRE(orch != nullptr);

  astl::IMetricManager* metric_manager_interface = orch->GetMetricManager().get();
  REQUIRE(metric_manager_interface != nullptr);

  auto* metric_mgr = dynamic_cast<astl::MetricManager*>(metric_manager_interface);
  REQUIRE(metric_mgr != nullptr);

  auto cfg = std::make_unique<astl::MetricConfig>("test_metric", "unit-test metric", ASTL_UNITS_CELSIUS,
                                                  ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
                                                  astl::CollectorType::SCMI, astl::NullOperationBuilder{});
  REQUIRE(cfg != nullptr);

  auto metric = std::make_unique<astl::SampledValueMetric>(cfg.get(),  // const MetricConfig*
                                                           tgt,        // const ITarget*
                                                           nullptr);   // IProcessedSampleSink*
  REQUIRE(metric != nullptr);

  astl::MetricManagerTestAccessor::InjectMetric(*metric_mgr, std::move(metric), std::move(cfg), tgt);

  // Act: serialize via the IMetricManager overload (dynamic_cast inside)
  std::stringstream cache_stream(std::ios::in | std::ios::out | std::ios::binary);
  const auto        status = astl::ProtobufSerDes::Serialize(*metric_manager_interface, cache_stream);

  // Assert
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(cache_stream.tellp() > 0);

  cache_stream.seekg(0);
  auto rebuilt_or_err =
      astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::IMetricManager>>(cache_stream, orch->GetTargets());
  REQUIRE(rebuilt_or_err.has_value());

  auto rebuilt_mgr = std::move(rebuilt_or_err.value());
  REQUIRE(rebuilt_mgr != nullptr);

  // Basic sanity: metric still available on the same target
  auto metrics_or_err = rebuilt_mgr->GetAvailableMetrics(tgt);
  REQUIRE(metrics_or_err.has_value());
  auto handles = metrics_or_err.value();
  REQUIRE(handles.size() == 1);

  const auto* rebuilt_handle = static_cast<const astl::MetricHandle*>(metrics_or_err.value()[0]);
  REQUIRE(rebuilt_handle != nullptr);
  REQUIRE(rebuilt_handle->config != nullptr);

  REQUIRE(rebuilt_handle->config->Name() == "test_metric");
  REQUIRE(rebuilt_handle->config->Description() == "unit-test metric");
  REQUIRE(rebuilt_handle->config->MetricType() == ASTL_METRIC_VALUE);
  REQUIRE(rebuilt_handle->config->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(rebuilt_handle->config->Units() == ASTL_UNITS_CELSIUS);

  REQUIRE(rebuilt_handle->target_to_metric_map.size() == 1);
  auto rebuilt_it = rebuilt_handle->target_to_metric_map.find(tgt);
  REQUIRE(rebuilt_it != rebuilt_handle->target_to_metric_map.end());
  REQUIRE(rebuilt_it->second != nullptr);
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

  // We can't see the internal maps directly here, but success implies:
  //  - BuildCapabilities ran
  //  - RebuildMetricHandles succeeded
  //  - RebuildOperationMap succeeded
}
