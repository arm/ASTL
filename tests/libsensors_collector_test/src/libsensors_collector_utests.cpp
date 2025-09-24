#include <algorithm>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "collector/libsensors_collector.hpp"
#include "common/libsensors.hpp"
#include "mock_libsensors.hpp"  // for global mock_libsensors object

using namespace std::chrono_literals;

using trompeloeil::_;

TEST_CASE("SensorsCollector::GetCapabilities", "[sensors_collector]") {
  astl::LibsensorsCollector collector;
  auto                      collector_capabilities = collector.GetCapabilities();
  REQUIRE(collector_capabilities.collector_type == astl::CollectorType::LIBSENSORS);
}

TEST_CASE("SensorsCollector::CollectOneSensor", "[sensors_collector]") {
  astl::LibsensorsCollector collector;
  MockRawSampleSink         sample_sink;
  REQUIRE_CALL(sample_sink, SinkRawSamples(_, _)).RETURN(ASTL_STATUS_SUCCESS);
  collector.SetRawSampleSink(&sample_sink);

  ALLOW_CALL(mock_libsensors, sensors_init(_)).RETURN(0);
  ALLOW_CALL(mock_libsensors, sensors_cleanup());

  std::string       chip1_prefix = "snsr";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 2};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};
  REQUIRE_CALL(mock_libsensors, sensors_get_value(&chip1, 1, _)).SIDE_EFFECT(*_3 = 42.0).RETURN(0);

  std::vector<std::unique_ptr<astl::Operation>> operations_on_sample;
  operations_on_sample.push_back(std::make_unique<astl::LibsensorsReadOperation>(&chip1, 1));

  astl::CollectionOperations collection_operations{
      .operationsBeforeStart = {},
      .operationsAtStart     = {},
      .operationsOnSample    = std::move(operations_on_sample),
      .operationsAtStop      = {},
      .samplingInterval      = 1000ms,
      .requirements          = astl::CollectorCapability{astl::CollectorType::LIBSENSORS}};
  astl::CollectionConfiguration config{
      nullptr, std::move(collection_operations),
      astl_collection_parameters_t{0, 0, ASTL_COLLECTION_MODE_IMMEDIATE, ASTL_COLLECTION_OPTIMIZATION_MEMORY}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();
  collector.ReadImmediate();
}
