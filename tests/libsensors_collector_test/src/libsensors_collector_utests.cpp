// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "libsensors/libsensors_collector.hpp"
#include "libsensors/libsensors_read_operation.hpp"
#include "mock_libsensors.hpp"  // for global mock_libsensors object

using namespace std::chrono_literals;

using trompeloeil::_;

TEST_CASE("SensorsCollector::GetCapabilities", "[sensors_collector]") {
  MockSensorsApiTestHarness harness;
  auto                      api = harness.api;
  REQUIRE(api->Ok());

  astl::LibsensorsCollector collector{api};
  auto                      collector_capabilities = collector.GetCapabilities();
  REQUIRE(collector_capabilities.collector_type == astl::CollectorType::LIBSENSORS);
}

TEST_CASE("SensorsCollector::CollectOneSensor", "[sensors_collector]") {
  MockSensorsApiTestHarness harness;
  auto                      api = harness.api;
  REQUIRE(api->Ok());
  astl::LibsensorsCollector collector{api};
  MockRawSampleSink         sample_sink;
  REQUIRE_CALL(sample_sink, SinkRawSamples(_, _)).RETURN(ASTL_STATUS_SUCCESS);
  collector.SetRawSampleSink(&sample_sink);

  auto& mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_init(_)).RETURN(0);
  ALLOW_CALL(*mock_libsensors, sensors_cleanup());

  std::string       chip1_prefix = "snsr";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 2};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(&chip1, 1, _)).SIDE_EFFECT(*_3 = 42.0).RETURN(0);

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
      astl_collection_params_t{0, ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, 0, ASTL_COLLECTION_MODE_IMMEDIATE}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();
  collector.ReadImmediate();
}

TEST_CASE("LibsensorsCollector StopCollection in IMMEDIATE mode", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  astl::LibsensorsCollector collector{harness.api};
  MockRawSampleSink         sample_sink;
  collector.SetRawSampleSink(&sample_sink);

  astl::CollectionOperations    ops{.operationsBeforeStart = {},
                                    .operationsAtStart     = {},
                                    .operationsOnSample    = {},
                                    .operationsAtStop      = {},
                                    .samplingInterval      = 1000ms,
                                    .requirements = astl::CollectorCapability{astl::CollectorType::LIBSENSORS}};
  astl::CollectionConfiguration config{
      nullptr, std::move(ops),
      astl_collection_params_t{0, ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, 0, ASTL_COLLECTION_MODE_IMMEDIATE}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();
  auto status = collector.StopCollection();
  REQUIRE(status == ASTL_STATUS_SUCCESS);
}

TEST_CASE("LibsensorsCollector StopCollection in SNAPSHOT mode", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  astl::LibsensorsCollector collector{harness.api};
  MockRawSampleSink         sample_sink;
  collector.SetRawSampleSink(&sample_sink);
  ALLOW_CALL(sample_sink, SinkRawSamples(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  std::string       chip1_prefix = "snsr";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 2};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};
  ALLOW_CALL(*mock_libsensors, sensors_get_value(&chip1, 1, _)).SIDE_EFFECT(*_3 = 55.0).RETURN(0);

  std::vector<std::unique_ptr<astl::Operation>> operations_on_sample;
  operations_on_sample.push_back(std::make_unique<astl::LibsensorsReadOperation>(&chip1, 1));

  astl::CollectionOperations    ops{.operationsBeforeStart = {},
                                    .operationsAtStart     = {},
                                    .operationsOnSample    = std::move(operations_on_sample),
                                    .operationsAtStop      = {},
                                    .samplingInterval      = 1000ms,
                                    .requirements = astl::CollectorCapability{astl::CollectorType::LIBSENSORS}};
  astl::CollectionConfiguration config{
      nullptr, std::move(ops),
      astl_collection_params_t{0, ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, 0, ASTL_COLLECTION_MODE_SNAPSHOT}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();
  auto status = collector.StopCollection();
  REQUIRE(status == ASTL_STATUS_SUCCESS);
}

TEST_CASE("LibsensorsCollector StopCollection in SAMPLING mode", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  astl::LibsensorsCollector collector{harness.api};
  MockRawSampleSink         sample_sink;
  collector.SetRawSampleSink(&sample_sink);

  astl::CollectionOperations    ops{.operationsBeforeStart = {},
                                    .operationsAtStart     = {},
                                    .operationsOnSample    = {},
                                    .operationsAtStop      = {},
                                    .samplingInterval      = 100ms,
                                    .requirements = astl::CollectorCapability{astl::CollectorType::LIBSENSORS}};
  astl::CollectionConfiguration config{
      nullptr, std::move(ops),
      astl_collection_params_t{0, ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, 0, ASTL_COLLECTION_MODE_SAMPLING}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();
  auto status = collector.StopCollection();
  REQUIRE(status == ASTL_STATUS_SUCCESS);
}

TEST_CASE("LibsensorsCollector StartCollection with bad configuration", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  astl::LibsensorsCollector collector{harness.api};
  auto                      status = collector.StartCollection();
  REQUIRE(status == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("LibsensorsCollector Pause and Resume", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  astl::LibsensorsCollector collector{harness.api};
  MockRawSampleSink         sample_sink;
  collector.SetRawSampleSink(&sample_sink);
  trompeloeil::sequence seq;
  REQUIRE_CALL(sample_sink, SinkRawSamples(_, _))
      .IN_SEQUENCE(seq)
      .WITH(_2.size() == 1)
      .WITH(_2[0].operation_id == astl::kPauseResumeOperationId)
      .WITH(std::get<uint64_t>(_2[0].value.value) == 0)
      .WITH(_2[0].raw_tick > 0)
      .RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(sample_sink, SinkRawSamples(_, _))
      .IN_SEQUENCE(seq)
      .WITH(_2.size() == 1)
      .WITH(_2[0].operation_id == astl::kPauseResumeOperationId)
      .WITH(std::get<uint64_t>(_2[0].value.value) == 1)
      .WITH(_2[0].raw_tick > 0)
      .RETURN(ASTL_STATUS_SUCCESS);

  astl::CollectionOperations    ops{.operationsBeforeStart = {},
                                    .operationsAtStart     = {},
                                    .operationsOnSample    = {},
                                    .operationsAtStop      = {},
                                    .samplingInterval      = 100ms,
                                    .requirements = astl::CollectorCapability{astl::CollectorType::LIBSENSORS}};
  astl::CollectionConfiguration config{
      nullptr, std::move(ops),
      astl_collection_params_t{0, ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, 0, ASTL_COLLECTION_MODE_SAMPLING}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();

  REQUIRE(collector.PauseCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ResumeCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.ReadImmediate() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("LibsensorsCollector StopCollection is idempotent", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  astl::LibsensorsCollector collector{harness.api};
  MockRawSampleSink         sample_sink;
  collector.SetRawSampleSink(&sample_sink);

  astl::CollectionOperations    ops{.operationsBeforeStart = {},
                                    .operationsAtStart     = {},
                                    .operationsOnSample    = {},
                                    .operationsAtStop      = {},
                                    .samplingInterval      = 100ms,
                                    .requirements = astl::CollectorCapability{astl::CollectorType::LIBSENSORS}};
  astl::CollectionConfiguration config{
      nullptr, std::move(ops),
      astl_collection_params_t{0, ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY, 0, ASTL_COLLECTION_MODE_SAMPLING}
  };

  collector.ConfigureCollection(std::move(config));
  collector.StartCollection();
  REQUIRE(collector.StopCollection() == ASTL_STATUS_SUCCESS);
  REQUIRE(collector.StopCollection() == ASTL_STATUS_COLLECTION_ALREADY_STOPPED);
}
