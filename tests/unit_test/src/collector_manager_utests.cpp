// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "collector/collector_builder.hpp"
#include "collector/collector_manager.hpp"
#include "topology/procfs_target.hpp"
#include "topology/scmi_target.hpp"

TEST_CASE("CollectorManager::RegisterRawSampleSink", "[collector_manager]") {
  // create a collector manager with an empty map of target-collector
  astl::CollectorManager collector_manager{{}};

  auto mock_sink  = std::make_unique<MockRawSampleSink>();
  auto mock_sink2 = std::make_unique<MockRawSampleSink>();

  SECTION("Register a valid sample sink") {
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
  }

  SECTION("Register two sample sinks") {
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink2.get()) == ASTL_STATUS_SUCCESS);
  }

  SECTION("Register a null sample sink") {
    REQUIRE(collector_manager.RegisterRawSampleSink(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("Unregister a valid sample sink") {
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.UnregisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
  }

  SECTION("Unregister a sample sink that was not registered") {
    REQUIRE(collector_manager.UnregisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_INTERNAL_ERROR);
  }

  SECTION("Unregister a null raw sample sink") {
    REQUIRE(collector_manager.UnregisterRawSampleSink(nullptr) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("Unregister a sample sink that was registered") {
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink2.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.UnregisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.UnregisterRawSampleSink(mock_sink2.get()) == ASTL_STATUS_SUCCESS);
  }

  SECTION("Unregister a sample sink twice") {
    REQUIRE(collector_manager.RegisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.UnregisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_SUCCESS);
    REQUIRE(collector_manager.UnregisterRawSampleSink(mock_sink.get()) == ASTL_STATUS_INTERNAL_ERROR);
  }
}

TEST_CASE("CollectorManager::ReportCollectionCapabilities", "[collector_manager]") {
  // create a collector manager with an empty map of target-collector

  auto mock_target          = std::make_unique<MockTarget>();
  auto mock_target2         = std::make_unique<MockTarget>();
  auto mock_scmi_collector  = std::make_unique<MockCollector>();
  auto mock_scmi_collector2 = std::make_unique<MockCollector>();
  auto mock_mmio_collector  = std::make_unique<MockCollector>();

  astl::CollectorCapability scmi_capabilities{astl::CollectorType::SCMI};
  astl::CollectorCapability mmio_capabilities{astl::CollectorType::PROCFS};

  // when CollectorManager is instantiated, it should set itself as the raw sample sink for each collector
  ALLOW_CALL(*mock_scmi_collector, SetRawSampleSink(trompeloeil::_));
  ALLOW_CALL(*mock_scmi_collector2, SetRawSampleSink(trompeloeil::_));
  ALLOW_CALL(*mock_mmio_collector, SetRawSampleSink(trompeloeil::_));

  // Set up the mock collectors to return the capabilities
  ALLOW_CALL(*mock_scmi_collector, GetCapabilities()).RETURN(scmi_capabilities);
  ALLOW_CALL(*mock_scmi_collector2, GetCapabilities()).RETURN(scmi_capabilities);
  ALLOW_CALL(*mock_mmio_collector, GetCapabilities()).RETURN(mmio_capabilities);

  SECTION("Report capabilities for a single target with 2 SCMI and 1 PROCFS collectors") {
    std::vector<std::unique_ptr<astl::ICollector>> collectors;
    collectors.push_back(std::move(mock_scmi_collector));
    collectors.push_back(std::move(mock_scmi_collector2));
    collectors.push_back(std::move(mock_mmio_collector));
    std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;
    collectors_map[mock_target.get()] = std::move(collectors);

    astl::CollectorManager collector_manager{std::move(collectors_map)};
    auto                   capabilities_map = collector_manager.ReportCollectionCapabilities();
    REQUIRE(capabilities_map.size() == 1);  // one supported target
    REQUIRE(capabilities_map.contains(mock_target.get()));
    REQUIRE(capabilities_map[mock_target.get()].size() >= 2);  // maybe SCMI shows up twice, maybe only once.
    std::vector<astl::CollectorCapability> expected_capabilities = {
        astl::CollectorCapability{astl::CollectorType::SCMI}, astl::CollectorCapability{astl::CollectorType::PROCFS}};
    for (const auto expected_cap : expected_capabilities) {
      auto collector_type_it =
          std::find_if(capabilities_map[mock_target.get()].begin(), capabilities_map[mock_target.get()].end(),
                       [&expected_cap](const astl::CollectorCapability& cap) {
                         return expected_cap.collector_type == cap.collector_type;
                       });
      REQUIRE(collector_type_it != capabilities_map[mock_target.get()].end());
    }
  }

  SECTION("Report capabilities for 2 targets - one with 1 PROCFS, one with 2 SCMI") {
    std::vector<std::unique_ptr<astl::ICollector>> collectors_t1;
    collectors_t1.push_back(std::move(mock_scmi_collector));   // cppcheck-suppress accessMoved
    collectors_t1.push_back(std::move(mock_scmi_collector2));  // cppcheck-suppress accessMoved
    std::vector<std::unique_ptr<astl::ICollector>> collectors_t2;
    collectors_t2.push_back(std::move(mock_mmio_collector));  // cppcheck-suppress accessMoved
    std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;
    collectors_map[mock_target.get()]  = std::move(collectors_t1);
    collectors_map[mock_target2.get()] = std::move(collectors_t2);

    astl::CollectorManager collector_manager{std::move(collectors_map)};
    auto                   capabilities_map = collector_manager.ReportCollectionCapabilities();
    REQUIRE(capabilities_map.size() == 2);  // two supported targets
    REQUIRE(capabilities_map.contains(mock_target.get()));
    REQUIRE(!capabilities_map[mock_target.get()].empty());  // maybe one SCMI collector for target 1, maybe 2
    REQUIRE(capabilities_map[mock_target.get()].size() <= 2);
    for (const auto& cap : capabilities_map[mock_target.get()]) {
      REQUIRE(cap.collector_type == astl::CollectorType::SCMI);
    }
    REQUIRE(capabilities_map[mock_target2.get()].size() == 1);
    for (const auto& cap : capabilities_map[mock_target2.get()]) {
      REQUIRE(cap.collector_type == astl::CollectorType::PROCFS);
    }
  }

  SECTION("Report capabilities for a target with no collectors") {
    auto                                           empty_target = std::make_unique<MockTarget>();
    std::vector<std::unique_ptr<astl::ICollector>> empty_collectors;
    std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;
    collectors_map[empty_target.get()] = std::move(empty_collectors);

    astl::CollectorManager collector_manager{std::move(collectors_map)};
    auto                   capabilities_map = collector_manager.ReportCollectionCapabilities();
    REQUIRE(!capabilities_map.contains(empty_target.get()));
  }
}

TEST_CASE("CollectorManager::BuildCollectorManager", "[collector_manager]") {
  // create a collector manager with an empty map of target->collector
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto                                        configuration = configuration_result.value();
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto                                        collector_manager = astl::BuildCollectorManager(targets, configuration);
  // with no targets, we should get a collector manager with no capabilities
  REQUIRE(collector_manager.has_value());
  REQUIRE(collector_manager.value()->ReportCollectionCapabilities().empty());
}

TEST_CASE("CollectorManager::BuildCollectorManager creates SCMI collectors for SCMI targets", "[collector_manager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto* target_0 = new astl::ScmiTarget{"scmi_tlm-0", "test target 0", "tlm-0", nullptr};
  auto* target_1 = new astl::ScmiTarget{"scmi_tlm-1", "test target 1", "tlm-1", nullptr};
  targets.emplace_back(target_0);
  targets.emplace_back(target_1);

  auto collector_manager = astl::BuildCollectorManager(targets, configuration);
  REQUIRE(collector_manager.has_value());

  auto capabilities_map = collector_manager.value()->ReportCollectionCapabilities();
  REQUIRE(capabilities_map.size() == 2);
  REQUIRE(capabilities_map.contains(target_0));
  REQUIRE(capabilities_map.contains(target_1));
  REQUIRE(capabilities_map.at(target_0).size() == 1);
  REQUIRE(capabilities_map.at(target_1).size() == 1);
  REQUIRE(capabilities_map.at(target_0).front().collector_type == astl::CollectorType::SCMI);
  REQUIRE(capabilities_map.at(target_1).front().collector_type == astl::CollectorType::SCMI);
}

TEST_CASE("CollectorManager::BuildCollectorManager creates procfs collectors for procfs targets",
          "[collector_manager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto* procfs_target = new astl::ProcfsTarget{"procfs", "test procfs target", "/proc"};
  targets.emplace_back(procfs_target);

  auto collector_manager = astl::BuildCollectorManager(targets, configuration);
  REQUIRE(collector_manager.has_value());

  auto capabilities_map = collector_manager.value()->ReportCollectionCapabilities();
  REQUIRE(capabilities_map.size() == 1);
  REQUIRE(capabilities_map.contains(procfs_target));
  REQUIRE(capabilities_map.at(procfs_target).size() == 1);
  REQUIRE(capabilities_map.at(procfs_target).front().collector_type == astl::CollectorType::PROCFS);
}

TEST_CASE("CollectorManager::BuildCollectorManager uses SCMI target metadata for sysfs subdirectories",
          "[collector_manager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto* target = new astl::ScmiTarget{"package0 power telemetry", "discovered test target", "tlm-0", nullptr};
  targets.emplace_back(target);

  auto collector_manager = astl::BuildCollectorManager(targets, configuration);
  REQUIRE(collector_manager.has_value());
  auto capabilities_map = collector_manager.value()->ReportCollectionCapabilities();
  REQUIRE(capabilities_map.size() == 1);
  REQUIRE(capabilities_map.contains(target));
  REQUIRE(capabilities_map.at(target).size() == 1);
  REQUIRE(capabilities_map.at(target).front().collector_type == astl::CollectorType::SCMI);
}

TEST_CASE("CollectorManager::BuildCollectorManager rejects SCMI targets without SCMI-specific metadata",
          "[collector_manager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto* target = new astl::Target{"tlm-0", "serialized test target", astl::CollectorType::SCMI, nullptr};
  targets.emplace_back(target);

  auto collector_manager = astl::BuildCollectorManager(targets, configuration);
  REQUIRE_FALSE(collector_manager.has_value());
  REQUIRE(collector_manager.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}

TEST_CASE("CollectorManager::BuildCollectorManager rejects unsupported collector types", "[collector_manager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("unsupported-target", "unsupported target",
                                                   astl::CollectorType::UNKNOWN, nullptr));

  auto collector_manager = astl::BuildCollectorManager(targets, configuration);
  REQUIRE_FALSE(collector_manager.has_value());
  REQUIRE(collector_manager.error() == ASTL_STATUS_NOT_SUPPORTED);
}

TEST_CASE("CollectorManager with no collectors", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;
  astl::CollectorManager collector_manager{std::move(collectors_map)};

  auto mock_target = std::make_unique<MockTarget>();

  SECTION("ConfigureCollectionOnTarget with no collectors") {
    astl_collection_params_t   collection_params{};
    astl::CollectionOperations operations{.operationsBeforeStart = {},
                                          .operationsAtStart     = {},
                                          .operationsOnSample    = {},
                                          .operationsAtStop      = {},
                                          .samplingInterval      = std::chrono::milliseconds{100},
                                          .requirements = {astl::CollectorCapability{astl::CollectorType::SCMI}}};
    REQUIRE(collector_manager.ConfigureCollectionOnTarget(mock_target.get(), collection_params,
                                                          std::move(operations)) == ASTL_STATUS_NO_TARGET_FOUND);
  }

  SECTION("StartOnTarget with no collectors") {
    REQUIRE(collector_manager.StartOnTarget(mock_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("PauseOnTarget with no collectors") {
    REQUIRE(collector_manager.PauseOnTarget(mock_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("ResumeOnTarget with no collectors") {
    REQUIRE(collector_manager.ResumeOnTarget(mock_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("ReadImmediateOnTarget with no collectors") {
    REQUIRE(collector_manager.ReadImmediateOnTarget(mock_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("StopOnTarget with no collectors") {
    REQUIRE(collector_manager.StopOnTarget(mock_target.get()) == ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
}

TEST_CASE("CollectorManager with no viable collectors", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;

  auto mock_target    = std::make_unique<MockTarget>();
  auto mock_collector = std::make_unique<MockCollector>();
  REQUIRE_CALL(*mock_collector, GetCapabilities()).RETURN(astl::CollectorCapability{astl::CollectorType::PROCFS});
  // we require SCMI, but collector provides only PROCFS, that's an error
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = std::chrono::milliseconds{100},
                                        .requirements = {astl::CollectorCapability{astl::CollectorType::SCMI}}};
  ALLOW_CALL(*mock_collector, SetRawSampleSink(trompeloeil::_));
  collectors_map[mock_target.get()].push_back(std::move(mock_collector));
  astl::CollectorManager collector_manager{std::move(collectors_map)};
  auto res = collector_manager.ConfigureCollectionOnTarget(mock_target.get(), astl_collection_params_t{},
                                                           std::move(operations));
  REQUIRE(res == ASTL_STATUS_INVALID_COLLECTION_MODE);
}

TEST_CASE("CollectorManager with one viable collector", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;

  auto mock_target    = std::make_unique<MockTarget>();
  auto mock_collector = std::make_unique<MockCollector>();
  REQUIRE_CALL(*mock_collector, GetCapabilities()).RETURN(astl::CollectorCapability{astl::CollectorType::SCMI});
  ALLOW_CALL(*mock_collector, SetRawSampleSink(trompeloeil::_));
  REQUIRE_CALL(*mock_collector, ConfigureCollection(trompeloeil::_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector, StartCollection()).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector, ReadImmediate()).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector, PauseCollection()).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector, ResumeCollection()).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector, StopCollection()).RETURN(ASTL_STATUS_SUCCESS);

  collectors_map[mock_target.get()].push_back(std::move(mock_collector));
  astl::CollectorManager collector_manager{std::move(collectors_map)};

  // we require SCMI, and the collector provides SCMI, that's good
  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = std::chrono::milliseconds{100},
                                        .requirements = {astl::CollectorCapability{astl::CollectorType::SCMI}}};
  auto res = collector_manager.ConfigureCollectionOnTarget(mock_target.get(), astl_collection_params_t{},
                                                           std::move(operations));
  REQUIRE(res == ASTL_STATUS_SUCCESS);

  REQUIRE(collector_manager.StartOnTarget(mock_target.get()) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.PauseOnTarget(mock_target.get()) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.ReadImmediateOnTarget(mock_target.get()) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.ResumeOnTarget(mock_target.get()) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.StopOnTarget(mock_target.get()) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("CollectorManager::ClearConfiguredCollections clears stale collector configurations", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;

  auto  mock_target        = std::make_unique<MockTarget>();
  auto  mock_collector     = std::make_unique<MockCollector>();
  auto* mock_collector_ptr = mock_collector.get();

  ALLOW_CALL(*mock_collector, SetRawSampleSink(trompeloeil::_));
  collectors_map[mock_target.get()].push_back(std::move(mock_collector));
  astl::CollectorManager collector_manager{std::move(collectors_map)};

  REQUIRE_CALL(*mock_collector_ptr, ClearCollectionState()).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.ClearConfiguredCollections() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("CollectorManager::ClearConfiguredCollections rejects active collections", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;

  auto  mock_target        = std::make_unique<MockTarget>();
  auto  mock_collector     = std::make_unique<MockCollector>();
  auto* mock_collector_ptr = mock_collector.get();

  REQUIRE_CALL(*mock_collector, GetCapabilities()).RETURN(astl::CollectorCapability{astl::CollectorType::SCMI});
  ALLOW_CALL(*mock_collector, SetRawSampleSink(trompeloeil::_));
  REQUIRE_CALL(*mock_collector, ConfigureCollection(trompeloeil::_)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(*mock_collector, StartCollection()).RETURN(ASTL_STATUS_SUCCESS);
  FORBID_CALL(*mock_collector_ptr, ClearCollectionState());

  collectors_map[mock_target.get()].push_back(std::move(mock_collector));
  astl::CollectorManager collector_manager{std::move(collectors_map)};

  astl::CollectionOperations operations{.operationsBeforeStart = {},
                                        .operationsAtStart     = {},
                                        .operationsOnSample    = {},
                                        .operationsAtStop      = {},
                                        .samplingInterval      = std::chrono::milliseconds{100},
                                        .requirements = {astl::CollectorCapability{astl::CollectorType::SCMI}}};
  REQUIRE(collector_manager.ConfigureCollectionOnTarget(mock_target.get(), astl_collection_params_t{},
                                                        std::move(operations)) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.StartOnTarget(mock_target.get()) == ASTL_STATUS_SUCCESS);
  REQUIRE(collector_manager.ClearConfiguredCollections() == ASTL_STATUS_COLLECTION_ALREADY_RUNNING);
}

TEST_CASE("CollectorManager::SinkRawSamples - no sinks", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;
  astl::CollectorManager collector_manager{std::move(collectors_map)};

  auto                              mock_target = std::make_unique<MockTarget>();
  std::vector<astl::RawSampledData> samples;

  // with no registered sinks, SinkRawSamples should succeed but do nothing
  REQUIRE(collector_manager.SinkRawSamples(mock_target.get(), samples) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("CollectorManager::SinkRawSamples - one sink", "collector_manager") {
  std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>> collectors_map;
  astl::CollectorManager collector_manager{std::move(collectors_map)};
  auto                   mock_sample_sink = std::make_unique<MockRawSampleSink>();
  REQUIRE_CALL(*mock_sample_sink, SinkRawSamples(trompeloeil::_, trompeloeil::_)).RETURN(ASTL_STATUS_SUCCESS);

  auto                              mock_target = std::make_unique<MockTarget>();
  std::vector<astl::RawSampledData> samples;
  REQUIRE(collector_manager.RegisterRawSampleSink(mock_sample_sink.get()) == ASTL_STATUS_SUCCESS);

  // with no registered sinks, SinkRawSamples should succeed but do nothing
  REQUIRE(collector_manager.SinkRawSamples(mock_target.get(), samples) == ASTL_STATUS_SUCCESS);
}
