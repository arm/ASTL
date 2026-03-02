// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <utility>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_test_hooks.h"
#include "common/metric_config.hpp"
#include "metric/counter.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"
#include "wrapper_utils.hpp"

using trompeloeil::_;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("astlGetMetricGroups", "[wrapper][Orchestrator][MetricGroups]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));
  // set up 2 metric groups
  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto  group0     = std::make_unique<astl::MetricGroup>("Group0", "Description0", std::vector<astl_metric_handle_t>{});
  auto *group0_ptr = group0.get();
  auto  group1     = std::make_unique<astl::MetricGroup>("Group1", "Description1", std::vector<astl_metric_handle_t>{});
  auto *group1_ptr = group1.get();
  std::vector<astl_metric_group_handle_t> available_groups;
  available_groups.push_back(group0->ToApiHandle());
  available_groups.push_back(group1->ToApiHandle());

  // with either target or no, same set of metric groups
  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups()).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroupProperties(_, _))
      .SIDE_EFFECT({
        if (_1 == group0_ptr->ToApiHandle()) {
          _2->_size         = sizeof(astl_metric_group_properties_t);
          _2->_handle       = group0_ptr->ToApiHandle();
          _2->_name         = group0_ptr->name.c_str();
          _2->_description  = group0_ptr->description.c_str();
          _2->_metric_count = static_cast<uint32_t>(group0_ptr->metrics.size());
        } else if (_1 == group1_ptr->ToApiHandle()) {
          _2->_size         = sizeof(astl_metric_group_properties_t);
          _2->_handle       = group1_ptr->ToApiHandle();
          _2->_name         = group1_ptr->name.c_str();
          _2->_description  = group1_ptr->description.c_str();
          _2->_metric_count = static_cast<uint32_t>(group1_ptr->metrics.size());
        }
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto topology_manager = std::make_unique<MockTopologyManager>();

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("[bad params][wrapper]") {
    REQUIRE(astlGetMetricGroupCount(nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricGroupCount(mock_target_handle, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t             metric_count{kJunk};
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(astlGetMetricGroupCount(invalid_target_handle, &metric_count) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    REQUIRE(metric_count == 0);
  }

  SECTION("[good params][wrapper][MetricGroups]") {
    uint32_t group_count{kJunk};
    REQUIRE(astlGetMetricGroupCount(mock_target_handle, &group_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);
  }

  SECTION("astlGetMetricGroups", "[MetricGroups][bad params][wrapper]") {
    REQUIRE(astlGetMetricGroups(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricGroups(mock_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    uint32_t group_count{kJunk};
    REQUIRE(astlGetMetricGroups(mock_target_handle, nullptr, &group_count) == ASTL_STATUS_BAD_ARGUMENT);
    std::vector<astl_metric_group_properties_t> groups{kAFew};
    REQUIRE(astlGetMetricGroups(mock_target_handle, groups.data(), nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    group_count     = kAFew;
    groups[0]._size = sizeof(astl_metric_group_properties_t) - 1;  // caller has old struct
    REQUIRE(astlGetMetricGroups(mock_target_handle, groups.data(), &group_count) ==
            ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION);
    group_count     = kAFew;
    groups[0]._size = sizeof(astl_metric_group_properties_t) + 1;  // caller has newer struct
    REQUIRE(astlGetMetricGroups(mock_target_handle, groups.data(), &group_count) ==
            ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION);
    // test for a buffer too small
    group_count     = 1;
    groups[0]._size = sizeof(astl_metric_group_properties_t);
    REQUIRE(astlGetMetricGroups(mock_target_handle, groups.data(), &group_count) ==
            ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL);
  }

  SECTION("astlGetMetrics", "[good params][wrapper][MetricGroups]") {
    uint32_t group_count{2};
    auto     groups = AllocateAstlVector<astl_metric_group_properties_t>(kAFew);
    REQUIRE(astlGetMetricGroups(mock_target_handle, groups.data(), &group_count) == ASTL_STATUS_SUCCESS);
    REQUIRE(std::string(groups[0]._name) == "Group0");
    REQUIRE(std::string(groups[1]._name) == "Group1");
  }
}

TEST_CASE("astlGetMetricGroupMetrics", "[MetricGroups][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->_handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  // set up 1 metric group
  auto  mock_metric     = std::make_unique<MockMetric>();
  auto *mock_metric_ptr = mock_metric.get();
  auto  metric_manager  = std::make_unique<MockMetricManager>();

  // duplicate metric in the group, but that's fine for this test.
  std::vector<astl_metric_handle_t>     available_metrics{mock_metric_ptr, mock_metric_ptr};
  std::span<const astl_metric_handle_t> available_metrics_span{available_metrics.data(), available_metrics.size()};
  auto group0 = std::make_unique<astl::MetricGroup>("Group0", "Description0", std::move(available_metrics));
  astl_metric_group_handle_t              group0_handle = group0->ToApiHandle();
  std::vector<astl_metric_group_handle_t> available_groups{group0_handle};

  // as the internal implementation would return it
  astl_metric_group_properties_t group0_properties{._size         = sizeof(astl_metric_group_properties_t),
                                                   ._handle       = group0_handle,
                                                   ._name         = group0->name.c_str(),
                                                   ._description  = group0->description.c_str(),
                                                   ._metric_count = static_cast<uint32_t>(group0->metrics.size()),
                                                   ._metrics      = nullptr};

  // with either target or no, same set of metric groups
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(available_metrics_span);
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});

  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups()).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group0_handle)).RETURN(available_metrics_span);
  ALLOW_CALL(*metric_manager, GetProperties(mock_metric_ptr, _))
      .SIDE_EFFECT({
        _2->_handle      = mock_metric_ptr;
        _2->_name        = "MockMetric";
        _2->_description = "A mock metric for testing";
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  auto topology_manager = std::make_unique<MockTopologyManager>();

  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto metrics               = AllocateAstlVector<astl_metric_properties_t>(kAFew);
  group0_properties._metrics = metrics.data();

  SECTION("[MetricGroups][bad params][wrapper]") {
    REQUIRE(astlGetMetricGroupMetrics(nullptr, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlGetMetricGroupMetrics(mock_target_handle, nullptr, nullptr) == ASTL_STATUS_BAD_ARGUMENT);

    REQUIRE(astlGetMetricGroupMetrics(mock_target_handle, nullptr, metrics.data()) == ASTL_STATUS_BAD_ARGUMENT);

    auto previous_count             = group0_properties._metric_count;
    group0_properties._metric_count = 0;  // invalid count to call MetricGroupMetrics
    REQUIRE(astlGetMetricGroupMetrics(mock_target_handle, &group0_properties, metrics.data()) ==
            ASTL_STATUS_BAD_ARGUMENT);
    group0_properties._metric_count = previous_count;

    // test struct version mismatches
    metrics[0]._size = sizeof(astl_metric_properties_t) - 1;  // caller has old struct
    REQUIRE(astlGetMetricGroupMetrics(mock_target_handle, &group0_properties, metrics.data()) ==
            ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION);
    metrics[0]._size = sizeof(astl_metric_properties_t) + 1;  // caller has newer struct
    REQUIRE(astlGetMetricGroupMetrics(mock_target_handle, &group0_properties, metrics.data()) ==
            ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION);
    // test for a buffer too small
    metrics[0]._size = sizeof(astl_metric_properties_t);
    // users _must_ provide a big enough buffer
  }

  /*** Configure collection on groups of metrics ***/
  SECTION("astlConfigureMetricGroupCollectionOnTarget", "[bad params][collect][MetricGroups][wrapper]") {
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(nullptr, nullptr, nullptr, 0) == ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(mock_target_handle, nullptr, nullptr, 0) ==
            ASTL_STATUS_BAD_ARGUMENT);
    astl_collection_parameters_t collection_params{
        ._size              = sizeof(astl_collection_parameters_t),
        ._sampling_interval = 100,
        ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
        ._optimization      = ASTL_COLLECTION_OPTIMIZATION_MEMORY,
    };
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(mock_target_handle, &collection_params, nullptr, 0) ==
            ASTL_STATUS_BAD_ARGUMENT);
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(mock_target_handle, &collection_params, nullptr, 0) ==
            ASTL_STATUS_BAD_ARGUMENT);

    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(invalid_target_handle, &collection_params, &group0_handle, 1) ==
            ASTL_STATUS_INVALID_TARGET_HANDLE);
  }

  SECTION("astlConfigureMetricGroupCollectionOnTarget", "[good params][collect][MetricGroups][wrapper]") {
    astl_collection_parameters_t collection_params{
        ._size              = sizeof(astl_collection_parameters_t),
        ._sampling_interval = 100,
        ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
        ._optimization      = ASTL_COLLECTION_OPTIMIZATION_MEMORY,
    };

    // no zero-length list of groups is allowed as configuration
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(mock_target_handle, &collection_params, &group0_handle, 0) ==
            ASTL_STATUS_SUCCESS);

    // configuring one group is allowed. very typical case.
    REQUIRE(astlConfigureMetricGroupCollectionOnTarget(mock_target_handle, &collection_params, &group0_handle, 1) ==
            ASTL_STATUS_SUCCESS);
  }

  SECTION("astlConfigureMetricGroupCollection", "[unimplemented for now][wrapper]") {
    REQUIRE(astlConfigureMetricGroupCollection(nullptr, nullptr, 0) == ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
