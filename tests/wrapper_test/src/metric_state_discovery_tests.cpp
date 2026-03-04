// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_test_hooks.h"
#include "common/astl_value.hpp"
#include "common/metric_config.hpp"
#include "metric/finite_set_metric.hpp"
#include "metric/metric_manager.hpp"
#include "metric/residency_metric.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"
#include "wrapper_utils.hpp"

using trompeloeil::_;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("astlGetMetricStatesOnTarget - Finite Set Metric", "[wrapper][MetricStateDiscovery][FiniteSet]") {
  // Setup target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  auto*                                       mock_target_raw    = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");

  // Create finite set metric configuration with specific values
  astl::FiniteSetMetricConfig::FiniteSet finite_set;
  finite_set.insert(astl::AstlValue{static_cast<uint32_t>(0)});
  finite_set.insert(astl::AstlValue{static_cast<uint32_t>(1)});
  finite_set.insert(astl::AstlValue{static_cast<uint32_t>(2)});

  astl::FiniteSetMetricConfig::ValueToInfoMap state_info;
  state_info[astl::AstlValue{static_cast<uint32_t>(0)}] = {"Idle", "CPU idle state"};
  state_info[astl::AstlValue{static_cast<uint32_t>(1)}] = {"Active", "CPU active state"};
  state_info[astl::AstlValue{static_cast<uint32_t>(2)}] = {"Sleep", "CPU sleep state"};

  auto finite_set_config = std::make_unique<astl::FiniteSetMetricConfig>(
      "StateMetric", "Test state metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT32, ASTL_METRIC_FINITE_SET_VALUE,
      ASTL_CATEGORY_UNCATEGORIZED, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{}, std::move(finite_set),
      std::move(state_info), astl::IdentityFormula{});

  // Create target-to-metric map and MetricHandle
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric_map;
  auto  finite_set_metric = std::make_unique<astl::FiniteSetMetric>(finite_set_config.get(), mock_target_raw, nullptr);
  auto* finite_set_metric_raw           = finite_set_metric.get();
  target_to_metric_map[mock_target_raw] = std::move(finite_set_metric);

  auto metric_handle_obj =
      std::make_unique<astl::MetricHandle>(std::move(finite_set_config), std::move(target_to_metric_map));
  astl_metric_handle_t metric_handle = metric_handle_obj.get();

  // Setup metric manager
  auto                              metric_manager    = std::make_unique<MockMetricManager>();
  std::vector<astl_metric_handle_t> available_metrics = {metric_handle};
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(metric_handle, mock_target_raw)).RETURN(finite_set_metric_raw);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  mock_targets.push_back(std::move(mock_target));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("Bad parameters") {
    uint32_t                             state_count = 3;
    std::vector<astl_state_properties_t> states(3);
    states[0]._size = sizeof(astl_state_properties_t);

    REQUIRE(astlGetMetricStatesOnTarget(nullptr, nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, nullptr, nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), nullptr) ==
            ASTL_STATUS_BAD_ARGUMENT);

    state_count = 0;
    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("Buffer too small") {
    uint32_t                             state_count = 2;  // Only space for 2, but metric has 3
    std::vector<astl_state_properties_t> states(2);
    states[0]._size = sizeof(astl_state_properties_t);

    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL);
    REQUIRE(state_count == 0);
  }

  SECTION("Valid request returns state names and values") {
    uint32_t                             state_count = 3;
    std::vector<astl_state_properties_t> states(3);
    states[0]._size = sizeof(astl_state_properties_t);

    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(state_count == 3);

    // Verify that all states have both name and value populated
    bool found_idle   = false;
    bool found_active = false;
    bool found_sleep  = false;

    for (uint32_t i = 0; i < state_count; ++i) {
      REQUIRE(states[i]._size == sizeof(astl_state_properties_t));
      REQUIRE(states[i]._name != nullptr);

      std::string name(states[i]._name);
      if (name == "Idle") {
        REQUIRE(states[i]._value.ui32 == 0);
        REQUIRE(states[i]._description != nullptr);
        REQUIRE(std::string(states[i]._description) == "CPU idle state");
        found_idle = true;
      } else if (name == "Active") {
        REQUIRE(states[i]._value.ui32 == 1);
        REQUIRE(states[i]._description != nullptr);
        REQUIRE(std::string(states[i]._description) == "CPU active state");
        found_active = true;
      } else if (name == "Sleep") {
        REQUIRE(states[i]._value.ui32 == 2);
        REQUIRE(states[i]._description != nullptr);
        REQUIRE(std::string(states[i]._description) == "CPU sleep state");
        found_sleep = true;
      }
    }

    REQUIRE(found_idle);
    REQUIRE(found_active);
    REQUIRE(found_sleep);
  }

  SECTION("Struct size validation") {
    uint32_t                             state_count = 3;
    std::vector<astl_state_properties_t> states(3);

    // Old struct version
    states[0]._size = sizeof(astl_state_properties_t) - 1;
    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);

    // New struct version
    states[0]._size = sizeof(astl_state_properties_t) + 1;
    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
  }
}

TEST_CASE("astlGetMetricStatesOnTarget - Residency Metric", "[wrapper][MetricStateDiscovery][Residency]") {
  // Setup target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  auto*                                       mock_target_raw    = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");

  // Create residency metric configuration
  astl::ResidencyMetricConfig::StateToInfoMap state_info;
  state_info["C0"] =
      astl::ResidencyMetricConfig::StateInfo{"C0", "CPU fully active", 1000000, astl::NullOperationBuilder{}};
  state_info["C1"] =
      astl::ResidencyMetricConfig::StateInfo{"C1", "CPU clock gated", 1000000, astl::NullOperationBuilder{}};
  state_info["C6"] =
      astl::ResidencyMetricConfig::StateInfo{"C6", "CPU deep power-down", 1000000, astl::NullOperationBuilder{}};

  auto residency_config = std::make_unique<astl::ResidencyMetricConfig>(
      "CPUResidency", "CPU C-State residency", ASTL_UNITS_SECONDS, ASTL_VALUE_UINT64, ASTL_METRIC_RESIDENCY,
      ASTL_CATEGORY_UNCATEGORIZED, astl::CollectorType::UNKNOWN, std::move(state_info), std::nullopt,
      astl::IdentityFormula{});

  std::vector<astl::ResidencyMetricConfig::StateInfo> state_configs;
  state_configs.push_back({"C0", "CPU fully active", 1000000, astl::NullOperationBuilder{}});
  state_configs.push_back({"C1", "CPU clock-gated idle state", 1000000, astl::NullOperationBuilder{}});
  state_configs.push_back({"C6", "CPU deep power-down", 1000000, astl::NullOperationBuilder{}});

  // Create target-to-metric map and MetricHandle
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric_map;
  auto  residency_metric     = std::make_unique<astl::ResidencyMetric>(residency_config.get(), std::move(state_configs),
                                                                       mock_target_raw, nullptr);
  auto* residency_metric_raw = residency_metric.get();
  target_to_metric_map[mock_target_raw] = std::move(residency_metric);

  auto metric_handle_obj =
      std::make_unique<astl::MetricHandle>(std::move(residency_config), std::move(target_to_metric_map));
  astl_metric_handle_t metric_handle = metric_handle_obj.get();

  // Setup metric manager
  auto                              metric_manager    = std::make_unique<MockMetricManager>();
  std::vector<astl_metric_handle_t> available_metrics = {metric_handle};
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(metric_handle, mock_target_raw)).RETURN(residency_metric_raw);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  mock_targets.push_back(std::move(mock_target));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("Valid request returns state names in state_configs order") {
    uint32_t                             state_count = 3;
    std::vector<astl_state_properties_t> states(3);
    states[0]._size = sizeof(astl_state_properties_t);

    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(state_count == 3);

    // Verify state names are in the same order as state_configs
    REQUIRE(states[0]._size == sizeof(astl_state_properties_t));
    REQUIRE(states[0]._name != nullptr);
    REQUIRE(std::string(states[0]._name) == "C0");
    REQUIRE(states[0]._description != nullptr);
    REQUIRE(std::string(states[0]._description) == "CPU fully active");

    REQUIRE(states[1]._size == sizeof(astl_state_properties_t));
    REQUIRE(states[1]._name != nullptr);
    REQUIRE(std::string(states[1]._name) == "C1");
    REQUIRE(states[1]._description != nullptr);
    REQUIRE(std::string(states[1]._description) == "CPU clock-gated idle state");

    REQUIRE(states[2]._size == sizeof(astl_state_properties_t));
    REQUIRE(states[2]._name != nullptr);
    REQUIRE(std::string(states[2]._name) == "C6");
    REQUIRE(states[2]._description != nullptr);
    REQUIRE(std::string(states[2]._description) == "CPU deep power-down");
  }
}

TEST_CASE("astlGetMetricStatesOnTarget - Residency Metric with Inferred State",
          "[wrapper][MetricStateDiscovery][Residency][InferredState]") {
  // Setup target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  auto*                                       mock_target_raw    = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");

  astl::ResidencyMetricConfig::StateToInfoMap state_info;
  state_info["C1"] =
      astl::ResidencyMetricConfig::StateInfo{"C1", "CPU clock-gated idle state", 1000000, astl::NullOperationBuilder{}};
  state_info["C6"] =
      astl::ResidencyMetricConfig::StateInfo{"C6", "CPU deep power-down", 1000000, astl::NullOperationBuilder{}};

  auto residency_config = std::make_unique<astl::ResidencyMetricConfig>(
      "CPUResidency", "CPU C-State residency", ASTL_UNITS_SECONDS, ASTL_VALUE_UINT64, ASTL_METRIC_RESIDENCY,
      ASTL_CATEGORY_UNCATEGORIZED, astl::CollectorType::UNKNOWN, std::move(state_info),
      astl::ResidencyMetricConfig::InferredStateInfo{"C0", "CPU fully active (inferred)"}, astl::IdentityFormula{});

  std::vector<astl::ResidencyMetricConfig::StateInfo> state_configs;
  state_configs.push_back({"C1", "CPU clock-gated idle state", 1000000, astl::NullOperationBuilder{}});
  state_configs.push_back({"C6", "CPU deep power-down", 1000000, astl::NullOperationBuilder{}});

  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric_map;
  auto  residency_metric     = std::make_unique<astl::ResidencyMetric>(residency_config.get(), std::move(state_configs),
                                                                       mock_target_raw, nullptr);
  auto* residency_metric_raw = residency_metric.get();
  target_to_metric_map[mock_target_raw] = std::move(residency_metric);

  auto metric_handle_obj =
      std::make_unique<astl::MetricHandle>(std::move(residency_config), std::move(target_to_metric_map));
  astl_metric_handle_t metric_handle = metric_handle_obj.get();

  auto                              metric_manager    = std::make_unique<MockMetricManager>();
  std::vector<astl_metric_handle_t> available_metrics = {metric_handle};
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(metric_handle, mock_target_raw)).RETURN(residency_metric_raw);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  mock_targets.push_back(std::move(mock_target));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("Inferred state name and description are populated") {
    uint32_t                             state_count = 3;  // C1 + C6 + inferred C0
    std::vector<astl_state_properties_t> states(3);
    states[0]._size = sizeof(astl_state_properties_t);

    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(state_count == 3);

    // First two states are C1 and C6 (from state_configs order)
    REQUIRE(std::string(states[0]._name) == "C1");
    REQUIRE(states[0]._description != nullptr);
    REQUIRE(std::string(states[0]._description) == "CPU clock-gated idle state");

    REQUIRE(std::string(states[1]._name) == "C6");
    REQUIRE(states[1]._description != nullptr);
    REQUIRE(std::string(states[1]._description) == "CPU deep power-down");

    // Last state is the inferred C0
    REQUIRE(std::string(states[2]._name) == "C0");
    REQUIRE(states[2]._description != nullptr);
    REQUIRE(std::string(states[2]._description) == "CPU fully active (inferred)");
  }
}

TEST_CASE("astlGetMetricStatesOnTarget - Unsupported Metric Type", "[wrapper][MetricStateDiscovery][InvalidType]") {
  // Setup target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  auto*                                       mock_target_raw    = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");

  // Create a non-stateful metric (DELTA type)
  auto  mock_metric     = std::make_unique<MockMetric>();
  auto* mock_metric_raw = mock_metric.get();
  ALLOW_CALL(*mock_metric, GetProperties(_))
      .SIDE_EFFECT({
        _1->_size        = sizeof(astl_metric_properties_t);
        _1->_metric_type = ASTL_METRIC_DELTA;  // Not a finite set or residency metric
        _1->_name        = "DeltaMetric";
      })
      .RETURN(ASTL_STATUS_SUCCESS);

  astl_metric_handle_t metric_handle = mock_metric.get();

  // Create target-to-metric map and MetricHandle
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric_map;
  target_to_metric_map[mock_target_raw] = std::move(mock_metric);

  auto delta_config = std::make_unique<astl::MetricConfig>(
      "DeltaMetric", "Test delta metric", ASTL_UNITS_NONE, ASTL_VALUE_UINT32, ASTL_CATEGORY_UNCATEGORIZED,
      ASTL_METRIC_DELTA, astl::CollectorType::UNKNOWN, astl::NullOperationBuilder{});

  auto metric_handle_obj =
      std::make_unique<astl::MetricHandle>(std::move(delta_config), std::move(target_to_metric_map));
  metric_handle = metric_handle_obj.get();

  // Setup metric manager
  auto                              metric_manager    = std::make_unique<MockMetricManager>();
  std::vector<astl_metric_handle_t> available_metrics = {metric_handle};
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(std::span(available_metrics));
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(metric_handle, mock_target_raw)).RETURN(mock_metric_raw);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");

  mock_targets.push_back(std::move(mock_target));
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("Unsupported metric type returns NOT_SUPPORTED") {
    uint32_t                             state_count = 3;
    std::vector<astl_state_properties_t> states(3);
    states[0]._size = sizeof(astl_state_properties_t);

    REQUIRE(astlGetMetricStatesOnTarget(mock_target_handle, metric_handle, states.data(), &state_count) ==
            ASTL_STATUS_NOT_SUPPORTED);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
