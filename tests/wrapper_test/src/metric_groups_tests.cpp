// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
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

TEST_CASE("Metric group discovery APIs", "[wrapper][Orchestrator][MetricGroups]") {
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
  std::vector<astl_metric_handle_t>     empty_metrics;
  std::span<const astl_metric_handle_t> empty_metrics_span{empty_metrics.data(), empty_metrics.size()};

  // with either target or no, same set of metric groups
  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups()).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group0_ptr->ToApiHandle())).RETURN(empty_metrics_span);
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group1_ptr->ToApiHandle())).RETURN(empty_metrics_span);
  ALLOW_CALL(*metric_manager, GetMetricGroupProperties(_, _))
      .SIDE_EFFECT({
        if (_1 == group0_ptr->ToApiHandle()) {
          _2->size        = sizeof(astl_metric_group_props_t);
          _2->handle      = group0_ptr->ToApiHandle();
          _2->name        = group0_ptr->name.c_str();
          _2->description = group0_ptr->description.c_str();
        } else if (_1 == group1_ptr->ToApiHandle()) {
          _2->size        = sizeof(astl_metric_group_props_t);
          _2->handle      = group1_ptr->ToApiHandle();
          _2->name        = group1_ptr->name.c_str();
          _2->description = group1_ptr->description.c_str();
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

  SECTION("astlGetMetricGroupCount rejects bad params", "[MetricGroups][wrapper]") {
    auto metric_group_count = nullptr;
    ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0,
                     .metric_group_count = metric_group_count);
    REQUIRE(astlGetMetricGroupCount(&params) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("astlGetMetricGroupCount reports all metric groups", "[MetricGroups][wrapper]") {
    uint32_t group_count{kJunk};
    ASTL_INIT_STRUCT(astl_get_metric_group_count_params_t, params, .flags = 0, .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroupCount(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);
  }

  SECTION("astlGetMetricGroupCountOnTarget rejects bad params", "[MetricGroups][wrapper]") {
    {
      auto target_handle      = nullptr;
      auto metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCountOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle      = mock_target_handle;
      auto        metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCountOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    uint32_t             metric_count{kJunk};
    int                  junk                  = 1;  // not null, but not a valid handle to a target
    astl_target_handle_t invalid_target_handle = static_cast<astl_target_handle_t>(&junk);
    {
      const auto* target_handle      = invalid_target_handle;
      auto*       metric_group_count = &metric_count;
      ASTL_INIT_STRUCT(astl_get_metric_group_count_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupCountOnTarget(&params) == ASTL_STATUS_INVALID_TARGET_HANDLE);
    }
    REQUIRE(metric_count == 0);
  }

  SECTION("astlGetMetricGroupCountOnTarget reports target metric groups", "[MetricGroups][wrapper]") {
    uint32_t group_count{kJunk};
    ASTL_INIT_STRUCT(astl_get_metric_group_count_on_target_params_t, params, .flags = 0,
                     .target_handle = mock_target_handle, .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroupCountOnTarget(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);
  }

  SECTION("astlGetMetricGroups returns all metric groups", "[MetricGroups][wrapper]") {
    uint32_t group_count{2};
    auto     groups = AllocateAstlVector<astl_metric_group_props_t>(kAFew);
    ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, params, .flags = 0, .metric_groups = groups.data(),
                     .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroups(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);
    REQUIRE(std::string(groups[0].name) == "Group0");
    REQUIRE(std::string(groups[1].name) == "Group1");
  }

  SECTION("astlGetMetricGroupsOnTarget rejects bad params", "[MetricGroups][wrapper]") {
    {
      auto target_handle      = nullptr;
      auto metric_groups      = nullptr;
      auto metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle      = mock_target_handle;
      auto        metric_groups      = nullptr;
      auto        metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    uint32_t group_count{kJunk};
    {
      const auto* target_handle      = mock_target_handle;
      auto        metric_groups      = nullptr;
      auto*       metric_group_count = &group_count;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    std::vector<astl_metric_group_props_t> groups{kAFew};
    {
      const auto* target_handle      = mock_target_handle;
      auto*       metric_groups      = groups.data();
      auto        metric_group_count = nullptr;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = metric_groups, .metric_group_count = metric_group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    group_count    = kAFew;
    groups[0].size = sizeof(astl_metric_group_props_t) - 1;
    {
      const auto* target_handle = mock_target_handle;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = groups.data(), .metric_group_count = &group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION);
    }
    group_count    = kAFew;
    groups[0].size = sizeof(astl_metric_group_props_t) + 1;
    {
      const auto* target_handle = mock_target_handle;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = groups.data(), .metric_group_count = &group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION);
    }
    group_count    = 1;
    groups[0].size = sizeof(astl_metric_group_props_t);
    {
      const auto* target_handle = mock_target_handle;
      ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = target_handle,
                       .metric_groups = groups.data(), .metric_group_count = &group_count);
      REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL);
    }
  }

  SECTION("astlGetMetricGroupsOnTarget returns target metric groups", "[MetricGroups][wrapper]") {
    uint32_t group_count{2};
    auto     groups = AllocateAstlVector<astl_metric_group_props_t>(kAFew);
    ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = mock_target_handle,
                     .metric_groups = groups.data(), .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);
    REQUIRE(std::string(groups[0].name) == "Group0");
    REQUIRE(std::string(groups[1].name) == "Group1");
  }
}

TEST_CASE("astlGetMetricGroupMetrics APIs", "[MetricGroups][wrapper]") {
  // set up target
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  auto*                                       mock_target_raw    = mock_target.get();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  const std::string                           mock_target_name{"MockTarget"};
  ALLOW_CALL(*mock_target, Name()).RETURN(mock_target_name);
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  // set up 1 metric group
  auto  mock_metric        = std::make_unique<MockMetric>();
  auto* mock_metric_ptr    = mock_metric.get();
  auto  metric_manager     = std::make_unique<MockMetricManager>();
  auto* metric_manager_ptr = metric_manager.get();

  // duplicate metric in the group, but that's fine for this test.
  std::vector<astl_metric_handle_t> available_metrics{mock_metric_ptr, mock_metric_ptr};
  auto group0 = std::make_unique<astl::MetricGroup>("Group0", "Description0", std::move(available_metrics));
  std::span<const astl_metric_handle_t> available_metrics_span{group0->metrics.data(), group0->metrics.size()};
  auto                                  group1 =
      std::make_unique<astl::MetricGroup>("Group1", "Description1", std::vector<astl_metric_handle_t>{mock_metric_ptr});
  astl_metric_group_handle_t              group0_handle = group0->ToApiHandle();
  astl_metric_group_handle_t              group1_handle = group1->ToApiHandle();
  std::vector<astl_metric_group_handle_t> available_groups{group0_handle, group1_handle};
  std::span<const astl_metric_handle_t>   group1_metrics_span{group1->metrics.data(), group1->metrics.size()};

  // as the internal implementation would return it
  astl_metric_group_props_t group0_properties{.size        = sizeof(astl_metric_group_props_t),
                                              .handle      = group0_handle,
                                              .name        = group0->name.c_str(),
                                              .description = group0->description.c_str()};
  astl_metric_group_props_t group1_properties{.size        = sizeof(astl_metric_group_props_t),
                                              .handle      = group1_handle,
                                              .name        = group1->name.c_str(),
                                              .description = group1->description.c_str()};

  // with either target or no, same set of metric groups
  ALLOW_CALL(*metric_manager, GetAvailableMetrics(_)).RETURN(available_metrics_span);
  ALLOW_CALL(*metric_manager, GetRequiredOperations(_, _))
      .RETURN(astl::CollectionOperations{
          {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});

  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups()).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group0_handle)).RETURN(available_metrics_span);
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group1_handle)).RETURN(group1_metrics_span);
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(mock_metric_ptr, mock_target_raw)).RETURN(mock_metric_ptr);
  ALLOW_CALL(*metric_manager, GetMetricGroupProperties(_, _))
      .SIDE_EFFECT({
        if (_1 == group0_handle) {
          *_2 = group0_properties;
        } else if (_1 == group1_handle) {
          *_2 = group1_properties;
        }
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, GetProperties(mock_metric_ptr, _))
      .SIDE_EFFECT({
        _2->size        = sizeof(astl_metric_props_t);
        _2->handle      = mock_metric_ptr;
        _2->name        = "MockMetric";
        _2->description = "A mock metric for testing";
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager, RemoveAllMetrics());
  ALLOW_CALL(*metric_manager, GetPauseResumeEventMetricOnTarget(_)).RETURN(nullptr);
  ALLOW_CALL(*metric_manager, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS);
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

  auto metrics = AllocateAstlVector<astl_metric_props_t>(kAFew);

  SECTION("[MetricGroups][bad params][wrapper][count]") {
    {
      const auto* target_handle = mock_target_handle;
      auto*       metric_count  = static_cast<uint32_t*>(nullptr);
      ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = group0_handle,
                       .metric_count = metric_count);
      REQUIRE(astlGetMetricGroupMetricCountOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      auto* metric_count = static_cast<uint32_t*>(nullptr);
      ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_params_t, params, .flags = 0,
                       .metric_group_handle = group0_handle, .metric_count = metric_count);
      REQUIRE(astlGetMetricGroupMetricCount(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      uint32_t metric_count = 0;
      ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_params_t, params, .flags = 0, .metric_group_handle = nullptr,
                       .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricCount(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle = mock_target_handle;
      uint32_t    metric_count  = 0;
      ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = nullptr, .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricCountOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
  }

  SECTION("[MetricGroups][bad params][wrapper]") {
    {
      auto  target_handle = nullptr;
      auto* metrics_ptr   = metrics.data();
      auto  metric_count  = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = group0_handle, .metrics = metrics_ptr,
                       .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      const auto* target_handle = mock_target_handle;
      auto*       metrics_ptr   = metrics.data();
      auto        metric_count  = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = nullptr, .metrics = metrics_ptr,
                       .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    {
      const auto* target_handle = mock_target_handle;
      auto*       metrics_ptr   = metrics.data();
      auto*       metric_count  = static_cast<uint32_t*>(nullptr);
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = group0_handle, .metrics = metrics_ptr,
                       .metric_count = metric_count);
      REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    {
      const auto* target_handle = mock_target_handle;
      auto        metric_count  = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = group0_handle, .metrics = nullptr,
                       .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    // test struct version mismatches
    metrics[0].size = sizeof(astl_metric_props_t) - 1;  // caller has old struct
    {
      const auto* target_handle = mock_target_handle;
      auto        metric_count  = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = group0_handle, .metrics = metrics.data(),
                       .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION);
    }
    metrics[0].size = sizeof(astl_metric_props_t) + 1;  // caller has newer struct
    {
      const auto* target_handle = mock_target_handle;
      auto        metric_count  = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .metric_group_handle = group0_handle, .metrics = metrics.data(),
                       .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION);
    }
    metrics[0].size = sizeof(astl_metric_props_t);
  }

  SECTION("[MetricGroups][bad params][wrapper][global]") {
    {
      auto* metrics_ptr  = metrics.data();
      auto  metric_count = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .metric_group_handle = nullptr,
                       .metrics = metrics_ptr, .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      auto* metrics_ptr  = metrics.data();
      auto* metric_count = static_cast<uint32_t*>(nullptr);
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .metric_group_handle = group0_handle,
                       .metrics = metrics_ptr, .metric_count = metric_count);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }
    {
      auto metric_count = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .metric_group_handle = group0_handle,
                       .metrics = nullptr, .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    metrics[0].size = sizeof(astl_metric_props_t) - 1;
    {
      auto metric_count = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .metric_group_handle = group0_handle,
                       .metrics = metrics.data(), .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION);
    }
    metrics[0].size = sizeof(astl_metric_props_t) + 1;
    {
      auto metric_count = static_cast<uint32_t>(metrics.size());
      ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .metric_group_handle = group0_handle,
                       .metrics = metrics.data(), .metric_count = &metric_count);
      REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION);
    }
    metrics[0].size = sizeof(astl_metric_props_t);
  }

  SECTION("astlGetMetricGroupsOnTarget populates returned count", "[MetricGroups][wrapper]") {
    uint32_t group_count{2};
    auto     groups = AllocateAstlVector<astl_metric_group_props_t>(2);
    ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = mock_target_handle,
                     .metric_groups = groups.data(), .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);
    REQUIRE(std::string(groups[0].name) == "Group0");
    REQUIRE(std::string(groups[1].name) == "Group1");
  }

  SECTION("astlGetMetricGroupMetricsOnTarget returns metrics for a discovered group", "[MetricGroups][wrapper]") {
    auto     groups = AllocateAstlVector<astl_metric_group_props_t>(2);
    uint32_t group_count{2};
    ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, groups_params, .flags = 0,
                     .target_handle = mock_target_handle, .metric_groups = groups.data(),
                     .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroupsOnTarget(&groups_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);

    uint32_t member_count = 0;
    ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_on_target_params_t, count_params, .flags = 0,
                     .target_handle = mock_target_handle, .metric_group_handle = groups[0].handle,
                     .metric_count = &member_count);
    REQUIRE(astlGetMetricGroupMetricCountOnTarget(&count_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(member_count == 2);

    auto metrics_buf = AllocateAstlVector<astl_metric_props_t>(member_count);
    ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, params, .flags = 0,
                     .target_handle = mock_target_handle, .metric_group_handle = groups[0].handle,
                     .metrics = metrics_buf.data(), .metric_count = &member_count);
    REQUIRE(astlGetMetricGroupMetricsOnTarget(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(member_count == 2);
    REQUIRE(std::string(metrics_buf[0].name) == "MockMetric");
  }

  SECTION("astlGetMetricGroupMetrics returns metrics for a discovered group", "[MetricGroups][wrapper]") {
    auto     groups = AllocateAstlVector<astl_metric_group_props_t>(2);
    uint32_t group_count{2};
    ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, groups_params, .flags = 0, .metric_groups = groups.data(),
                     .metric_group_count = &group_count);
    REQUIRE(astlGetMetricGroups(&groups_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(group_count == 2);

    uint32_t member_count = 0;
    ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_params_t, count_params, .flags = 0,
                     .metric_group_handle = groups[0].handle, .metric_count = &member_count);
    REQUIRE(astlGetMetricGroupMetricCount(&count_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(member_count == 2);

    auto metrics_buf = AllocateAstlVector<astl_metric_props_t>(member_count);
    ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0,
                     .metric_group_handle = groups[0].handle, .metrics = metrics_buf.data(),
                     .metric_count = &member_count);
    REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_SUCCESS);
    REQUIRE(member_count == 2);
    REQUIRE(std::string(metrics_buf[0].name) == "MockMetric");
  }

  SECTION("astlGetMetricGroupMetricCount APIs return member counts", "[MetricGroups][wrapper]") {
    uint32_t global_member_count = 0;
    ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_params_t, global_params, .flags = 0,
                     .metric_group_handle = group0_handle, .metric_count = &global_member_count);
    REQUIRE(astlGetMetricGroupMetricCount(&global_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(global_member_count == 2);

    uint32_t target_member_count = 0;
    ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_on_target_params_t, target_params, .flags = 0,
                     .target_handle = mock_target_handle, .metric_group_handle = group0_handle,
                     .metric_count = &target_member_count);
    REQUIRE(astlGetMetricGroupMetricCountOnTarget(&target_params) == ASTL_STATUS_SUCCESS);
    REQUIRE(target_member_count == 2);
  }

  SECTION("astlGetMetricGroupMetrics APIs report required buffer size", "[MetricGroups][wrapper]") {
    uint32_t member_count = 1;
    ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, params, .flags = 0, .metric_group_handle = group0_handle,
                     .metrics = metrics.data(), .metric_count = &member_count);
    REQUIRE(astlGetMetricGroupMetrics(&params) == ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL);
    REQUIRE(member_count == 2);

    member_count = 1;
    ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, on_target_params, .flags = 0,
                     .target_handle = mock_target_handle, .metric_group_handle = group0_handle,
                     .metrics = metrics.data(), .metric_count = &member_count);
    REQUIRE(astlGetMetricGroupMetricsOnTarget(&on_target_params) == ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL);
    REQUIRE(member_count == 2);
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
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_BAD_ARGUMENT);
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

    // configuring overlapping groups should deduplicate repeated metrics before collection.
    {
      REQUIRE_CALL(*metric_manager_ptr, GetRequiredOperations(_, _))
          .LR_WITH(_1.size() == 1)
          .RETURN(astl::CollectionOperations{
              {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});

      std::array<astl_metric_group_handle_t, 2> group_handles{group0_handle, group1_handle};
      const auto*                               target_handle         = mock_target_handle;
      auto*                                     collection_params_ptr = &collection_params;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, params, .flags = 0,
                       .target_handle = target_handle, .collection_params = collection_params_ptr,
                       .metric_group_handles = group_handles.data(), .metric_group_count = 2);
      REQUIRE(astlConfigureMetricGroupCollectionOnTarget(&params) == ASTL_STATUS_SUCCESS);
    }
  }

  SECTION("astlConfigureMetricGroupCollection", "[wrapper]") {
    {
      auto     collection_params    = nullptr;
      auto     metric_group_handles = nullptr;
      uint32_t metric_group_count   = 0;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_params_t, params, .flags = 0,
                       .collection_params = collection_params, .metric_group_handles = metric_group_handles,
                       .metric_group_count = metric_group_count);
      REQUIRE(astlConfigureMetricGroupCollection(&params) == ASTL_STATUS_BAD_ARGUMENT);
    }

    {
      astl_collection_params_t collection_params{
          .size              = sizeof(astl_collection_params_t),
          .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
          .sampling_interval = 100,
          .collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
      };
      int                        fake_group_token  = 0;
      astl_metric_group_handle_t fake_group_handle = &fake_group_token;
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_params_t, params, .flags = 0,
                       .collection_params = &collection_params, .metric_group_handles = &fake_group_handle,
                       .metric_group_count = 1);
      REQUIRE(astlConfigureMetricGroupCollection(&params) == ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE);
    }

    {
      astl_collection_params_t collection_params{
          .size              = sizeof(astl_collection_params_t),
          .flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,
          .sampling_interval = 100,
          .collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
      };

      REQUIRE_CALL(*metric_manager_ptr, GetRequiredOperations(_, _))
          .LR_WITH(_1.size() == 1)
          .RETURN(astl::CollectionOperations{
              {}, {}, {}, {}, std::chrono::milliseconds{100}, astl::CollectorCapability{astl::CollectorType::SCMI}});

      std::array<astl_metric_group_handle_t, 2> group_handles{group0_handle, group1_handle};
      ASTL_INIT_STRUCT(astl_configure_metric_group_collection_params_t, params, .flags = 0,
                       .collection_params = &collection_params, .metric_group_handles = group_handles.data(),
                       .metric_group_count = static_cast<uint32_t>(group_handles.size()));
      REQUIRE(astlConfigureMetricGroupCollection(&params) == ASTL_STATUS_SUCCESS);
    }
  }
}

TEST_CASE("astlGetMetricGroupsOnTarget returns group metadata without membership state", "[MetricGroups][wrapper]") {
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;
  auto                                        mock_target        = std::make_unique<MockTarget>();
  auto*                                       mock_target_raw    = mock_target.get();
  astl_target_handle_t                        mock_target_handle = mock_target.get();
  ALLOW_CALL(*mock_target, Name()).RETURN("MockTarget");
  ALLOW_CALL(*mock_target, GetProperties(_)).SIDE_EFFECT(_1->handle = mock_target_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(mock_target));

  auto  metric_manager = std::make_unique<MockMetricManager>();
  auto  metric0        = std::make_unique<MockMetric>();
  auto* metric0_ptr    = metric0.get();
  auto  metric1        = std::make_unique<MockMetric>();
  auto* metric1_ptr    = metric1.get();
  auto  group0 =
      std::make_unique<astl::MetricGroup>("Group0", "Description0", std::vector<astl_metric_handle_t>{metric0_ptr});
  auto group1 =
      std::make_unique<astl::MetricGroup>("Group1", "Description1", std::vector<astl_metric_handle_t>{metric1_ptr});
  auto*                                   group0_ptr = group0.get();
  auto*                                   group1_ptr = group1.get();
  std::vector<astl_metric_group_handle_t> available_groups{group0_ptr->ToApiHandle(), group1_ptr->ToApiHandle()};
  std::span<const astl_metric_handle_t>   group0_metrics_span{group0_ptr->metrics.data(), group0_ptr->metrics.size()};
  std::span<const astl_metric_handle_t>   group1_metrics_span{group1_ptr->metrics.data(), group1_ptr->metrics.size()};

  ALLOW_CALL(*metric_manager, GetMetricGroups(_)).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricGroups()).RETURN(std::span(available_groups));
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group0_ptr->ToApiHandle())).RETURN(group0_metrics_span);
  ALLOW_CALL(*metric_manager, GetMetricsInGroup(group1_ptr->ToApiHandle())).RETURN(group1_metrics_span);
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(metric0_ptr, mock_target_raw)).RETURN(metric0_ptr);
  ALLOW_CALL(*metric_manager, GetMetricOnTarget(metric1_ptr, mock_target_raw)).RETURN(metric1_ptr);
  ALLOW_CALL(*metric_manager, GetMetricGroupProperties(_, _))
      .SIDE_EFFECT({
        if (_1 == group0_ptr->ToApiHandle()) {
          _2->size        = sizeof(astl_metric_group_props_t);
          _2->handle      = group0_ptr->ToApiHandle();
          _2->name        = group0_ptr->name.c_str();
          _2->description = group0_ptr->description.c_str();
        } else if (_1 == group1_ptr->ToApiHandle()) {
          _2->size        = sizeof(astl_metric_group_props_t);
          _2->handle      = group1_ptr->ToApiHandle();
          _2->name        = group1_ptr->name.c_str();
          _2->description = group1_ptr->description.c_str();
        }
      })
      .RETURN(ASTL_STATUS_SUCCESS);
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
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t group_count{2};
  auto     groups = AllocateAstlVector<astl_metric_group_props_t>(2);

  ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, params, .flags = 0, .target_handle = mock_target_handle,
                   .metric_groups = groups.data(), .metric_group_count = &group_count);
  REQUIRE(astlGetMetricGroupsOnTarget(&params) == ASTL_STATUS_SUCCESS);
  REQUIRE(group_count == 2);
  REQUIRE(std::string(groups[0].name) == "Group0");
  REQUIRE(std::string(groups[1].name) == "Group1");
}

TEST_CASE("astlGetMetricGroupMetricsOnTarget filters group members to the requested target",
          "[MetricGroups][wrapper]") {
  std::vector<std::unique_ptr<astl::ITarget>> mock_targets;

  auto                 target0        = std::make_unique<MockTarget>();
  auto*                target0_raw    = target0.get();
  astl_target_handle_t target0_handle = target0_raw;
  ALLOW_CALL(*target0_raw, Name()).RETURN("Target0");
  ALLOW_CALL(*target0_raw, GetProperties(_)).SIDE_EFFECT(_1->handle = target0_handle).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(target0));

  auto  target1     = std::make_unique<MockTarget>();
  auto* target1_raw = target1.get();
  ALLOW_CALL(*target1_raw, Name()).RETURN("Target1");
  ALLOW_CALL(*target1_raw, GetProperties(_)).SIDE_EFFECT(_1->handle = target1_raw).RETURN(ASTL_STATUS_SUCCESS);
  mock_targets.push_back(std::move(target1));

  auto  metric0     = std::make_unique<MockMetric>();
  auto* metric0_raw = metric0.get();
  auto  metric1     = std::make_unique<MockMetric>();
  auto* metric1_raw = metric1.get();

  auto  metric_manager     = std::make_unique<MockMetricManager>();
  auto* metric_manager_raw = metric_manager.get();

  auto                                    group     = std::make_unique<astl::MetricGroup>("Group0", "Description0",
                                                                                          std::vector<astl_metric_handle_t>{metric0_raw, metric1_raw});
  auto*                                   group_raw = group.get();
  std::vector<astl_metric_group_handle_t> global_group_handles{group_raw->ToApiHandle()};
  std::vector<astl_metric_group_handle_t> target0_groups{group_raw->ToApiHandle()};
  std::vector<astl_metric_group_handle_t> target1_groups{group_raw->ToApiHandle()};
  std::span<const astl_metric_handle_t>   group_metrics_span{group->metrics.data(), group->metrics.size()};

  ALLOW_CALL(*metric_manager_raw, GetMetricGroups()).RETURN(std::span(global_group_handles));
  ALLOW_CALL(*metric_manager_raw, GetMetricGroups(target0_raw)).RETURN(std::span(target0_groups));
  ALLOW_CALL(*metric_manager_raw, GetMetricGroups(target1_raw)).RETURN(std::span(target1_groups));
  ALLOW_CALL(*metric_manager_raw, GetMetricsInGroup(group_raw->ToApiHandle())).RETURN(group_metrics_span);
  ALLOW_CALL(*metric_manager_raw, GetMetricOnTarget(metric0_raw, target0_raw)).RETURN(metric0_raw);
  ALLOW_CALL(*metric_manager_raw, GetMetricOnTarget(metric1_raw, target0_raw))
      .RETURN(std::unexpected(ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET));
  ALLOW_CALL(*metric_manager_raw, GetMetricOnTarget(metric0_raw, target1_raw))
      .RETURN(std::unexpected(ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET));
  ALLOW_CALL(*metric_manager_raw, GetMetricOnTarget(metric1_raw, target1_raw)).RETURN(metric1_raw);
  ALLOW_CALL(*metric_manager_raw, GetMetricGroupProperties(_, _))
      .SIDE_EFFECT({
        _2->size        = sizeof(astl_metric_group_props_t);
        _2->handle      = group_raw->ToApiHandle();
        _2->name        = group_raw->name.c_str();
        _2->description = group_raw->description.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager_raw, GetProperties(metric0_raw, _))
      .SIDE_EFFECT({
        _2->size        = sizeof(astl_metric_props_t);
        _2->handle      = metric0_raw;
        _2->name        = "Metric0";
        _2->description = "Metric0 description";
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager_raw, GetProperties(metric1_raw, _))
      .SIDE_EFFECT({
        _2->size        = sizeof(astl_metric_props_t);
        _2->handle      = metric1_raw;
        _2->name        = "Metric1";
        _2->description = "Metric1 description";
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager_raw, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager_raw, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*metric_manager_raw, RemoveAllMetrics());

  auto topology_manager  = std::make_unique<MockTopologyManager>();
  auto collector_manager = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);

  auto output_manager = std::make_unique<MockOutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(mock_targets));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t group_count{1};
  auto     groups = AllocateAstlVector<astl_metric_group_props_t>(1);
  ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, groups_params, .flags = 0,
                   .target_handle = target0_handle, .metric_groups = groups.data(), .metric_group_count = &group_count);
  REQUIRE(astlGetMetricGroupsOnTarget(&groups_params) == ASTL_STATUS_SUCCESS);
  REQUIRE(group_count == 1);

  uint32_t target_metric_count = 0;
  ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_on_target_params_t, count_params, .flags = 0,
                   .target_handle = target0_handle, .metric_group_handle = groups[0].handle,
                   .metric_count = &target_metric_count);
  REQUIRE(astlGetMetricGroupMetricCountOnTarget(&count_params) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_metric_count == 1);

  auto metrics = AllocateAstlVector<astl_metric_props_t>(target_metric_count);
  ASTL_INIT_STRUCT(astl_get_metric_group_metrics_on_target_params_t, metrics_params, .flags = 0,
                   .target_handle = target0_handle, .metric_group_handle = groups[0].handle, .metrics = metrics.data(),
                   .metric_count = &target_metric_count);
  REQUIRE(astlGetMetricGroupMetricsOnTarget(&metrics_params) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_metric_count == 1);
  REQUIRE(std::string(metrics[0].name) == "Metric0");

  uint32_t global_group_count{1};
  auto     global_groups = AllocateAstlVector<astl_metric_group_props_t>(1);
  ASTL_INIT_STRUCT(astl_get_metric_groups_params_t, global_groups_params, .flags = 0,
                   .metric_groups = global_groups.data(), .metric_group_count = &global_group_count);
  REQUIRE(astlGetMetricGroups(&global_groups_params) == ASTL_STATUS_SUCCESS);
  REQUIRE(global_group_count == 1);

  uint32_t global_metric_count = 0;
  ASTL_INIT_STRUCT(astl_get_metric_group_metric_count_params_t, global_count_params, .flags = 0,
                   .metric_group_handle = global_groups[0].handle, .metric_count = &global_metric_count);
  REQUIRE(astlGetMetricGroupMetricCount(&global_count_params) == ASTL_STATUS_SUCCESS);
  REQUIRE(global_metric_count == 2);

  auto global_metrics = AllocateAstlVector<astl_metric_props_t>(global_metric_count);
  ASTL_INIT_STRUCT(astl_get_metric_group_metrics_params_t, global_metrics_params, .flags = 0,
                   .metric_group_handle = global_groups[0].handle, .metrics = global_metrics.data(),
                   .metric_count = &global_metric_count);
  REQUIRE(astlGetMetricGroupMetrics(&global_metrics_params) == ASTL_STATUS_SUCCESS);
  REQUIRE(global_metric_count == 2);
  REQUIRE(std::string(global_metrics[0].name) == "Metric0");
  REQUIRE(std::string(global_metrics[1].name) == "Metric1");
}

// NOLINTEND(readability-function-cognitive-complexity)
