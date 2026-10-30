// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <fstream>
#include <sstream>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "collector/collector_backend.hpp"
#include "collector/collector_builder.hpp"
#include "metric/metric_builder.hpp"
#include "numa/numa_collector.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "topology/topology_builder.hpp"
namespace {
using astl::CollectionConfiguration;
using astl::CollectionOperations;
using astl::CollectorCapability;
using astl::CollectorType;
using astl::FileInterface;
using astl::IRawSampleSink;
using astl::ITarget;
using astl::NumaCollector;
using astl::NumaField;
using astl::NumaReadOperation;
using astl::RawSampledData;
using astl::Target;
struct Sink : IRawSampleSink {
  std::vector<RawSampledData> samples;
  auto SinkRawSamples(const ITarget* /*target*/, std::span<RawSampledData> values) -> astl_status_code override {
    samples.assign(values.begin(), values.end());
    return ASTL_STATUS_SUCCESS;
  }
};
struct Fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("astl-numa-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempFileGuard cleanup{root};
  Target        target{"numa", "test", CollectorType::NUMA};
  Sink          sink;
  NumaCollector collector{
      FileInterface{root},
      {"node0", "node2"}
  };
  Fixture() {
    Write("node0", 100, 50);
    Write("node2", 20, 30);
    collector.SetRawSampleSink(&sink);
  }
  void Write(const std::string& node, uint64_t local, uint64_t remote) const {
    std::filesystem::create_directories(root / node);
    std::ofstream file{root / node / "numastat"};
    file << "numa_hit " << local << "\nnuma_miss " << remote << "\nnuma_foreign 0\ninterleave_hit 0\nlocal_node "
         << local << "\nother_node " << remote << "\n";
  }
  void Configure(astl_collection_mode_t mode = ASTL_COLLECTION_MODE_IMMEDIATE) {
    CollectionOperations ops{.operationsBeforeStart{},
                             .operationsAtStart{},
                             .operationsOnSample{},
                             .operationsAtStop{},
                             .samplingInterval{},
                             .requirements{CollectorCapability{CollectorType::NUMA}}};
    for (const auto field : {NumaField::HIT, NumaField::MISS, NumaField::INTERLEAVE_HIT, NumaField::LOCAL_NODE,
                             NumaField::OTHER_NODE, NumaField::LOCAL_PERCENT, NumaField::REMOTE_PERCENT}) {
      ops.operationsOnSample.push_back(std::make_unique<NumaReadOperation>(field, ""));
    }
    ops.operationsOnSample.push_back(std::make_unique<NumaReadOperation>(NumaField::ALLOCATIONS, "node2"));
    astl_collection_params_t params{};
    params.collection_mode   = mode;
    params.sampling_interval = 1000;
    REQUIRE(collector.ConfigureCollection(CollectionConfiguration{&target, std::move(ops), params}) ==
            ASTL_STATUS_SUCCESS);
  }
  void Read() {
    sink.samples.clear();
    REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  }
};
}  // namespace
TEST_CASE("NUMA parser rejects incomplete duplicate and invalid fields", "[numa]") {
  const std::string valid = "numa_hit 1\nnuma_miss 2\ninterleave_hit 3\nlocal_node 4\nother_node 5\nnuma_foreign 9\n";
  REQUIRE(astl::numa::Parse(valid));
  CHECK(*astl::numa::Parse(valid) == astl::numa::Counts{1, 2, 3, 4, 5});
  CHECK_FALSE(astl::numa::Parse("numa_hit 1\n"));
  CHECK_FALSE(astl::numa::Parse(valid + "numa_hit 1\n"));
  for (const auto* invalid : {"-1", "18446744073709551616", "12x", "1 extra"}) {
    auto text = valid;
    text.replace(text.find("hit 1") + 4, 1, invalid);
    CHECK_FALSE(astl::numa::Parse(text));
  }
}
TEST_CASE("NUMA emits page deltas and absolute basis point percentages", "[numa]") {
  Fixture fixture;
  fixture.Configure();
  fixture.Read();
  CHECK(fixture.sink.samples.empty());
  fixture.Write("node0", 160, 70);
  fixture.Write("node2", 40, 30);
  fixture.Read();
  REQUIRE(fixture.sink.samples.size() == 8);
  const std::array<uint64_t, 8> expected{80, 20, 0, 80, 20, 8000, 2000, 20};
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(fixture.sink.samples[i].value == astl::AstlValue{expected.at(i)});
  }
  fixture.Read();
  REQUIRE(fixture.sink.samples.size() == 8);
  for (const auto& sample : fixture.sink.samples) {
    CHECK(sample.value == astl::AstlValue{uint64_t{0}});
  }
}
TEST_CASE("NUMA reset and read failure rebaseline without unsigned spikes", "[numa]") {
  Fixture fixture;
  fixture.Configure();
  fixture.Read();
  fixture.Write("node0", 1, 1);
  fixture.Write("node2", 21, 31);
  fixture.Read();
  REQUIRE(fixture.sink.samples.size() == 1);  // unaffected node2 still has its own interval
  CHECK(fixture.sink.samples[0].value == astl::AstlValue{uint64_t{2}});
  std::ofstream{fixture.root / "node0" / "numastat"} << "broken\n";
  CHECK(fixture.collector.ReadImmediate() == ASTL_STATUS_BAD_CONFIGURATION);
  fixture.Write("node0", 2, 2);
  fixture.Read();
  CHECK(fixture.sink.samples.empty());
  fixture.Write("node0", 3, 3);
  fixture.Read();
  REQUIRE(fixture.sink.samples.size() == 8);
}
TEST_CASE("NUMA snapshot and restart establish new baselines", "[numa]") {
  Fixture fixture;
  fixture.Configure(ASTL_COLLECTION_MODE_SNAPSHOT);
  REQUIRE(fixture.collector.StartCollection() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.empty());
  fixture.Write("node0", 110, 50);
  REQUIRE(fixture.collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.sink.samples.size() == 8);
  CHECK(fixture.sink.samples[0].value == astl::AstlValue{uint64_t{10}});
  fixture.sink.samples.clear();
  fixture.Configure(ASTL_COLLECTION_MODE_SNAPSHOT);
  REQUIRE(fixture.collector.StartCollection() == ASTL_STATUS_SUCCESS);
  CHECK(fixture.sink.samples.empty());
  REQUIRE(fixture.collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(fixture.collector.ClearCollectionState() == ASTL_STATUS_SUCCESS);
}
TEST_CASE("NUMA discovery skips unsupported directories and preserves sparse node IDs", "[numa]") {
  Fixture fixture;
  fixture.Write("node9x", 1, 2);
  std::filesystem::create_directories(fixture.root / "node");
  auto nodes = astl::numa::Discover(astl::FileInterface{fixture.root});
  REQUIRE(nodes);
  CHECK(*nodes == std::vector<std::string>{"node0", "node2"});
  auto missing = astl::numa::Discover(astl::FileInterface{fixture.root / "absent"});
  REQUIRE(missing);
  CHECK(missing->empty());
}
TEST_CASE("NUMA discovery rejects partial node statistics", "[numa]") {
  Fixture          fixture;
  astl_status_code expected_status = ASTL_STATUS_BAD_CONFIGURATION;
  SECTION("Malformed statistics on a valid node") {
    std::ofstream{fixture.root / "node2" / "numastat"} << "numa_hit invalid\n";
  }
  SECTION("Unreadable statistics on a valid node") {
    std::filesystem::remove(fixture.root / "node2" / "numastat");
    std::string text;
    expected_status = astl::FileInterface{fixture.root}.Read("node2/numastat", text);
    REQUIRE(expected_status != ASTL_STATUS_SUCCESS);
  }
  auto nodes = astl::numa::Discover(astl::FileInterface{fixture.root});
  REQUIRE_FALSE(nodes);
  CHECK(nodes.error() == expected_status);
}
TEST_CASE("NUMA backend integrates with common builders and platform selection", "[numa]") {
  Fixture     fixture;
  EnvVarGuard root{astl::EnvVar::ASTL_NUMA_ROOT, fixture.root.string()};
  EnvVarGuard selected{astl::EnvVar::ASTL_COLLECTORS, "numa"};
  auto        config = astl::AstlConfiguration::CreateConfiguration();
#if !defined(ASTL_INCLUDE_NUMA)
  REQUIRE_FALSE(config);
  CHECK(config.error() == ASTL_STATUS_BAD_CONFIGURATION);
  return;
#endif
  REQUIRE(config);
  CHECK(config->numa_root_path == fixture.root);
  auto topology = astl::BuildTopologyManager(*config, std::nullopt);
  REQUIRE(topology);
#if defined(ASTL_INCLUDE_NUMA)
  const auto& targets = (*topology)->GetTargets();
  REQUIRE(targets.size() == 1);
  auto metrics = astl::BuildMetricManager(targets, *config, std::nullopt);
  REQUIRE(metrics);
  auto available = (*metrics)->GetAvailableMetrics(targets[0].get());
  REQUIRE(available);
  CHECK(available->size() == 9);
  auto counters = (*metrics)->GetAvailableCounters(targets[0].get());
  REQUIRE(counters);
  CHECK(counters->size() == 9);
  std::stringstream topology_stream;
  std::stringstream metric_stream;
  REQUIRE(astl::ProtobufSerDes::Serialize(**topology, topology_stream) == ASTL_STATUS_SUCCESS);
  REQUIRE(astl::ProtobufSerDes::Serialize(**metrics, metric_stream) == ASTL_STATUS_SUCCESS);
  auto restored_topology = astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::ITopologyManager>>(topology_stream);
  REQUIRE(restored_topology);
  const auto& restored_targets = (*restored_topology)->GetTargets();
  REQUIRE(restored_targets.size() == 1);
  CHECK(restored_targets[0]->GetCollectorType() == astl::CollectorType::NUMA);
  auto restored_metrics =
      astl::ProtobufSerDes::Deserialize<std::unique_ptr<astl::IMetricManager>>(metric_stream, restored_targets);
  REQUIRE(restored_metrics);
  auto restored_available = (*restored_metrics)->GetAvailableMetrics(restored_targets[0].get());
  REQUIRE(restored_available);
  CHECK(restored_available->size() == 9);
  config->load_file_path = "saved-numa.astl";
  REQUIRE(astl::BuildCollectorManager(restored_targets, *config));
#else
  CHECK((*topology)->GetTargets().empty());
#endif
}

TEST_CASE("NUMA resume excludes paused allocations", "[numa]") {
  Fixture fixture;
  fixture.Configure();
  REQUIRE(fixture.collector.StartCollection() == ASTL_STATUS_SUCCESS);
  fixture.Read();
  REQUIRE(fixture.collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  fixture.Write("node0", 900, 900);
  REQUIRE(fixture.collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  fixture.Read();
  CHECK(fixture.sink.samples.empty());
  fixture.Write("node0", 901, 900);
  fixture.Read();
  REQUIRE(fixture.sink.samples.size() == 8);
  CHECK(fixture.sink.samples[0].get<uint64_t>() == 1);
  REQUIRE(fixture.collector.StopCollection() == ASTL_STATUS_SUCCESS);
}
TEST_CASE("NUMA aggregation rejects overflow and changed node sets", "[numa]") {
  const auto           maximum = std::numeric_limits<uint64_t>::max();
  astl::numa::Snapshot previous{
      {"node0", {}},
      {"node2", {}}
  };
  astl::numa::Snapshot current{
      {"node0", {maximum, 0, 0, 0, 0}},
      {"node2", {1, 0, 0, 0, 0}      }
  };
  CHECK_FALSE(astl::numa::Delta(previous, current, ""));
  current.erase("node2");
  CHECK_FALSE(astl::numa::Delta(previous, current, ""));
}
