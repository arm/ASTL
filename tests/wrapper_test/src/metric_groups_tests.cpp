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
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));
  // set up 2 metric groups
  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto  group0     = std::make_unique<astl::MetricGroup>("Group0", "Description0", std::vector<astl_metric_handle_t>{});
  auto* group0_ptr = group0.get();
  auto  group1     = std::make_unique<astl::MetricGroup>("Group1", "Description1", std::vector<astl_metric_handle_t>{});
  auto* group1_ptr = group1.get();
  std::vector<astl_metric_group_handle_t> available_groups;
  available_groups.push_back(group0->ToApiHandle());
  available_groups.push_back(group1->ToApiHandle());

  // with either target or no, same set of metric groups
  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups()).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroupProperties(_, _))
      .SIDE_EFFECT({
        if (_1 == group0_ptr->ToApiHandle()) {
          _2->size         = sizeof(astl_metric_group_props_t);
          _2->handle       = group0_ptr->ToApiHandle();
          _2->name         = group0_ptr->name.c_str();
          _2->description  = group0_ptr->description.c_str();
          _2->metric_count = static_cast<uint32_t>(group0_ptr->metrics.size());
        } else if (_1 == group1_ptr->ToApiHandle()) {
          _2->size         = sizeof(astl_metric_group_props_t);
          _2->handle       = group1_ptr->ToApiHandle();
          _2->name         = group1_ptr->name.c_str();
          _2->description  = group1_ptr->description.c_str();
          _2->metric_count = static_cast<uint32_t>(group1_ptr->metrics.size());
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
    {
      auto target_handle      = nullptr;
      auto metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCount(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle      = mock_target_handle;
      auto        metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCount(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    uint32_t             metric_count{kJunk};
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    {
      const auto* target_handle      = invalid_target_handle;
      auto*       metric_group_count = &metric_count;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCount(&params) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    }
    REQUIRE(metric_count == 0);
  }

  SECTION("[good params][wrapper][MetricGroups]") {
    uint32_t group_count{kJunk};
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCount(&params) == ASTL_STATUS_SUCCESS);
    }
    REQUIRE(group_count == 2);
  }

  SECTION("astlGetMetricGroups", "[MetricGroups][bad params][wrapper]") {
    {
      auto target_handle      = nullptr;
      auto metric_groups      = nullptr;
      auto metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle      = mock_target_handle;
      auto        metric_groups      = nullptr;
      auto        metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    uint32_t group_count{kJunk};
    {
      const auto* target_handle      = mock_target_handle;
      auto        metric_groups      = nullptr;
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    std::vector<astl_metric_group_props_t> groups{kAFew};
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_groups      = groups.data();
      auto        metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    group_count    = kAFew;
    groups[0].size = sizeof(astl_metric_group_props_t) - 1;  // caller has old struct
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_groups      = groups.data();
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION);
    }
    group_count    = kAFew;
    groups[0].size = sizeof(astl_metric_group_props_t) + 1;  // caller has newer struct
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_groups      = groups.data();
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION);
    }
    // test for a buffer too small
    group_count    = 1;
    groups[0].size = sizeof(astl_metric_group_props_t);
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_groups      = groups.data();
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL);
    }
  }

  SECTION("astlGetMetrics", "[good params][wrapper][MetricGroups]") {
    uint32_t group_count{2};
    auto     groups = AllocateAstlVector<astl_metric_group_props_t>(kAFew);
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_groups      = groups.data();
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_SUCCESS);
    }
    REQUIRE(std::string(groups[0].name) == "Group0");
    REQUIRE(std::string(groups[1].name) == "Group1");
  }
}

TEST_CASE("astlGetMetricGroupMetrics", "[MetricGroups][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  // set up 1 metric group
  auto  mock_metric     = std::make_unique<MockMetric>();
  auto* mock_metric_ptr = mock_metric.get();
  auto  metric_manager  = std::make_unique<MockMetricManager>();

  // duplicate metric in the group, but that's fine for this test.
  std::vector<astl_metric_handle_t>     available_metrics{mock_metric_ptr, mock_metric_ptr};
  std::span<const astl_metric_handle_t> available_metrics_span{available_metrics.data(), available_metrics.size()};
  auto group0 = std::make_unique<astl::MetricGroup>("Group0", "Description0", std::move(available_metrics));
  astl_metric_group_handle_t              group0_handle = group0->ToApiHandle();
  std::vector<astl_metric_group_handle_t> available_groups{group0_handle};

  // as the internal implementation would return it
  astl_metric_group_props_t group0_properties{.size         = sizeof(astl_metric_group_props_t),
                                              .handle       = group0_handle,
                                              .name         = group0->name.c_str(),
                                              .description  = group0->description.c_str(),
                                              .metric_count = static_cast<uint32_t>(group0->metrics.size()),
                                              .metrics      = nullptr};

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
        _2->handle      = mock_metric_ptr;
        _2->name        = "MockMetric";
        _2->description = "A mock metric for testing";
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

  auto metrics              = AllocateAstlVector<astl_metric_props_t>(kAFew);
  group0_properties.metrics = metrics.data();

  SECTION("[MetricGroups][bad params][wrapper]") {
    {
      auto target_handle = nullptr;
      auto metric_group  = nullptr;
      auto metrics_ptr   = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group = metric_group, .metrics = metrics_ptr);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle = mock_target_handle;
      auto        metric_group  = nullptr;
      auto        metrics_ptr   = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group = metric_group, .metrics = metrics_ptr);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    {
      const auto* target_handle = mock_target_handle;
      auto        metric_group  = nullptr;
      auto*       metrics_ptr   = metrics.data();
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group = metric_group, .metrics = metrics_ptr);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    auto previous_count            = group0_properties.metric_count;
    group0_properties.metric_count = 0;  // invalid count to call MetricGroupMetrics
    {
      const auto* target_handle = mock_target_handle;
      auto*       metric_group  = &group0_properties;
      auto*       metrics_ptr   = metrics.data();
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group = metric_group, .metrics = metrics_ptr);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    group0_properties.metric_count = previous_count;

    // test struct version mismatches
    metrics[0].size = sizeof(astl_metric_props_t) - 1;  // caller has old struct
    {
      const auto* target_handle = mock_target_handle;
      auto*       metric_group  = &group0_properties;
      auto*       metrics_ptr   = metrics.data();
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group = metric_group, .metrics = metrics_ptr);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION);
    }
    metrics[0].size = sizeof(astl_metric_props_t) + 1;  // caller has newer struct
    {
      const auto* target_handle = mock_target_handle;
      auto*       metric_group  = &group0_properties;
      auto*       metrics_ptr   = metrics.data();
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_group = metric_group, .metrics = metrics_ptr);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION);
    }
    // test for a buffer too small
    metrics[0].size = sizeof(astl_metric_props_t);
    // users _must_ provide a big enough buffer
  }

  /*** Configure collection on groups of metrics ***/
  SECTION("astlConfigureMetricGroupCollectionOnTarget", "[bad params][collect][MetricGroups][wrapper]") {
    {
      auto     target_handle        = nullptr;
      auto     collection_params    = nullptr;
      auto     metric_group_handles = nullptr;
      uint32_t metric_group_count   = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle        = mock_target_handle;
      auto        collection_params    = nullptr;
      auto        metric_group_handles = nullptr;
      uint32_t    metric_group_count   = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    astl_collection_params_t collection_params{
        .size  = sizeof(astl_collection_params_t),
        .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,

        .sampling_interval = 100,

        .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE,
    };
    {
      const auto* target_handle         = mock_target_handle;
      auto*       collection_params_ptr = &collection_params;
      auto        metric_group_handles  = nullptr;
      uint32_t    metric_group_count    = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params_ptr,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle         = mock_target_handle;
      auto*       collection_params_ptr = &collection_params;
      auto        metric_group_handles  = nullptr;
      uint32_t    metric_group_count    = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params_ptr,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    {
      const auto* target_handle         = invalid_target_handle;
      auto*       collection_params_ptr = &collection_params;
      auto*       metric_group_handles  = &group0_handle;
      uint32_t    metric_group_count    = 1;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params_ptr,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    }
  }

  SECTION("astlConfigureMetricGroupCollectionOnTarget", "[good params][collect][MetricGroups][wrapper]") {
    astl_collection_params_t collection_params{
        .size  = sizeof(astl_collection_params_t),
        .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,

        .sampling_interval = 100,

        .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE,
    };

    // no zero-length list of groups is allowed as configuration
    {
      const auto* target_handle         = mock_target_handle;
      auto*       collection_params_ptr = &collection_params;
      auto*       metric_group_handles  = &group0_handle;
      uint32_t    metric_group_count    = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params_ptr,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_SUCCESS);
    }

    // configuring one group is allowed. very typical case.
    {
      const auto* target_handle         = mock_target_handle;
      auto*       collection_params_ptr = &collection_params;
      auto*       metric_group_handles  = &group0_handle;
      uint32_t    metric_group_count    = 1;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params_ptr,
                       .metric_group_handles = metric_group_handles, .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_SUCCESS);
    }
  }

  SECTION("astlConfigureMetricGroupCollection", "[unimplemented for now][wrapper]") {
    {
      auto     collection_params    = nullptr;
      auto     metric_group_handles = nullptr;
      uint32_t metric_group_count   = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_params_t, params, .flags = 0,
                       .collection_params = collection_params, .metric_group_handles = metric_group_handles,
                       .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollection(&params) == ASTL_STATUS_NOT_IMPLEMENTED);
    }
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
