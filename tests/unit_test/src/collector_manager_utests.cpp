/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "collector/collector_builder.hpp"
#include "collector/collector_manager.hpp"

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
  // NOLINTBEGIN(readability-function-cognitive-complexity)

  // create a collector manager with an empty map of target-collector

  auto mock_target          = std::make_unique<MockTarget>();
  auto mock_target2         = std::make_unique<MockTarget>();
  auto mock_scmi_collector  = std::make_unique<MockCollector>();
  auto mock_scmi_collector2 = std::make_unique<MockCollector>();
  auto mock_mmio_collector  = std::make_unique<MockCollector>();

  astl::CollectorCapability scmi_capabilities{astl::CollectorType::SCMI};
  astl::CollectorCapability mmio_capabilities{astl::CollectorType::MMIO};

  // when CollectorManager is instantiated, it should set itself as the raw sample sink for each collector
  ALLOW_CALL(*mock_scmi_collector, SetRawSampleSink(trompeloeil::_));
  ALLOW_CALL(*mock_scmi_collector2, SetRawSampleSink(trompeloeil::_));
  ALLOW_CALL(*mock_mmio_collector, SetRawSampleSink(trompeloeil::_));

  // Set up the mock collectors to return the capabilities
  ALLOW_CALL(*mock_scmi_collector, GetCapabilities()).RETURN(scmi_capabilities);
  ALLOW_CALL(*mock_scmi_collector2, GetCapabilities()).RETURN(scmi_capabilities);
  ALLOW_CALL(*mock_mmio_collector, GetCapabilities()).RETURN(mmio_capabilities);

  SECTION("Report capabilities for a single target with 2 SCMI and 1 MMIO collectors") {
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
        astl::CollectorCapability{astl::CollectorType::SCMI}, astl::CollectorCapability{astl::CollectorType::MMIO}};
    for (const auto expected_cap : expected_capabilities) {
      auto collector_type_it =
          std::find_if(capabilities_map[mock_target.get()].begin(), capabilities_map[mock_target.get()].end(),
                       [&expected_cap](const astl::CollectorCapability& cap) {
                         return expected_cap.collector_type == cap.collector_type;
                       });
      REQUIRE(collector_type_it != capabilities_map[mock_target.get()].end());
    }
  }

  SECTION("Report capabilities for 2 targets - one with 1 MMIO, one with 2 SCMI") {
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
      REQUIRE(cap.collector_type == astl::CollectorType::MMIO);
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

  // NOLINTEND(readability-function-cognitive-complexity)
}

TEST_CASE("CollectorManager::BuildCollectorManager", "[collector_manager]") {
  // NOLINTBEGIN(readability-function-cognitive-complexity)

  // create a collector manager with an empty map of target->collector
  astl::AstlConfiguration                     configuration;
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto                                        collector_manager = astl::BuildCollectorManager(targets, configuration);
  // with no targets, we should get a collector manager with no capabilities
  REQUIRE(collector_manager.has_value());
  REQUIRE(collector_manager.value()->ReportCollectionCapabilities().empty());

  // NOLINTEND(readability-function-cognitive-complexity)
}
