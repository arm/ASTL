#include <google/protobuf/stubs/common.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>

#include "astl/astl_errors.h"
#include "capabilities.hpp"
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

static RawSampledData MakeSample(OperationId operation_id, AstlValue value, int64_t ts_us) {
  RawSampledData sample{operation_id, std::move(value)};
  sample.timestamp = SampleTimestamp{SampleTimestamp::duration{std::chrono::microseconds{ts_us}}};
  return sample;
}

static auto MakeTarget(std::string name, std::string description, astl::CollectorType collector_type,
                       std::optional<std::string> uuid = std::nullopt) -> std::unique_ptr<astl::Target> {
  return std::make_unique<astl::Target>(std::move(name), std::move(description), collector_type, nullptr,
                                        std::move(uuid));
}

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

  // Cleanup
  ifs.close();
  fs::remove(file_path, err_code);
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
  sample->set_uint32_value(7);  // set some value so VALUE_NOT_SET is not triggered first

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
