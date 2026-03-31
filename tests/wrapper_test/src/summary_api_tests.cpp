// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_summary_api.cpp
 * @brief Unit tests for the astlGetMetricStatisticsOnTarget C API
 *
 * These tests exercise the full C API path through the injected test orchestrator,
 * covering argument validation, struct versioning, supported/unsupported types,
 * and correct min/max/avg computation for integer and floating-point metrics.
 */

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "common/astl_value.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "wrapper_utils.hpp"

using trompeloeil::_;

/**
 * @brief Builds a fully wired test orchestrator suitable for astlGetMetricStatisticsOnTarget tests.
 *
 * @param mock_target           Unique pointer to a MockTarget; ownership is transferred.
 * @param mock_metric_manager   Unique pointer to a pre-configured MockMetricManager.
 * @return A pair of {orchestrator, kept-alive expectations}.
 */
static auto BuildTestOrchestrator(std::unique_ptr<MockTarget>        mock_target,
                                  std::unique_ptr<MockMetricManager> mock_metric_manager)
    -> std::pair<std::unique_ptr<astl::Orchestrator>, std::vector<expectation>> {
  auto                     topology_manager  = std::make_unique<MockTopologyManager>();
  auto                     collector_manager = std::make_unique<MockCollectorManager>();
  std::vector<expectation> expectations;
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(
      NAMED_ALLOW_CALL(*mock_metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(
      NAMED_ALLOW_CALL(*mock_metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(*mock_metric_manager, RemoveAllMetrics()));
  auto output_manager = std::make_unique<astl::OutputManager>();

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::move(mock_target));

  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(mock_metric_manager), std::move(output_manager), "");
  orchestrator->SetTargets(std::move(targets));
  return {std::move(orchestrator), std::move(expectations)};
}

// ---------------------------------------------------------------------------
// Argument validation & struct versioning (no orchestrator needed)
// ---------------------------------------------------------------------------

TEST_CASE("astlGetMetricStatisticsOnTarget - NULL arguments", "[summary_api]") {
  astl_metric_statistics_t summary{};
  summary.size  = sizeof(astl_metric_statistics_t);
  summary.flags = 0;
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);

  // NULL target handle
  REQUIRE(GetMetricStatisticsOnTarget(nullptr, nullptr, &summary) == ASTL_STATUS_BAD_ARGUMENT);

  // NULL metric handle (non-null target)
  REQUIRE(GetMetricStatisticsOnTarget(non_null_target, nullptr, &summary) == ASTL_STATUS_BAD_ARGUMENT);

  // NULL summary pointer
  REQUIRE(GetMetricStatisticsOnTarget(non_null_target, non_null_metric, nullptr) == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetMetricStatisticsOnTarget - invalid struct size", "[summary_api]") {
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);

  SECTION("Size too small") {
    astl_metric_statistics_t summary{};
    summary.size = sizeof(astl_metric_statistics_t) - 1;
    auto result  = GetMetricStatisticsOnTarget(non_null_target, non_null_metric, &summary);
    REQUIRE(result == ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
  }

  SECTION("Size too large") {
    astl_metric_statistics_t summary{};
    summary.size = sizeof(astl_metric_statistics_t) + 1;
    auto result  = GetMetricStatisticsOnTarget(non_null_target, non_null_metric, &summary);
    REQUIRE(result == ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
  }
}

TEST_CASE("astlGetMetricStatisticsOnTarget - invalid summary flags", "[summary_api]") {
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);

  SECTION("Unknown flag bit") {
    astl_metric_statistics_t summary{};
    summary.size  = sizeof(astl_metric_statistics_t);
    summary.flags = (1U << 7);
    auto result   = GetMetricStatisticsOnTarget(non_null_target, non_null_metric, &summary);
    REQUIRE(result == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("Mutually exclusive average-mode flags") {
    astl_metric_statistics_t summary{};
    summary.size  = sizeof(astl_metric_statistics_t);
    summary.flags = ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG | ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG;
    auto result   = GetMetricStatisticsOnTarget(non_null_target, non_null_metric, &summary);
    REQUIRE(result == ASTL_STATUS_BAD_ARGUMENT);
  }
}

// ---------------------------------------------------------------------------
// End-to-end tests with an injected orchestrator + mocked metric infrastructure
// ---------------------------------------------------------------------------

TEST_CASE("astlGetMetricStatisticsOnTarget - uint64 samples", "[summary_api]") {
  // --- Set up mock target ---
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"SummaryTarget"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  // --- Set up metric config + handle ---
  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["SummaryTarget"] = {0xABCD};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "TestMetric", "TestMetric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  // --- Set up mock metric manager ---
  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  // Mock metric GetProperties to report UINT64 / VALUE
  static std::string const metric_name{"TestMetric"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_UINT64;
        _1->metric_type = ASTL_METRIC_VALUE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  // --- Build orchestrator and inject ---
  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  auto*                    orchestrator_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  SECTION("No samples collected yields count 0") {
    astl_metric_statistics_t summary{};
    summary.size  = sizeof(astl_metric_statistics_t);
    summary.flags = 0;
    auto result   = GetMetricStatisticsOnTarget(target_handle, metric_handle.get(), &summary);
    // No data → count must be 0 (implementation may return SUCCESS with count==0)
    REQUIRE(result == ASTL_STATUS_SUCCESS);
    REQUIRE(summary.count == 0);
  }

  SECTION("Single sample") {
    std::vector<astl::ProcessedSampledData> samples;
    samples.emplace_back(astl::AstlValue{uint64_t{42}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});

    REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

    astl_metric_statistics_t summary{};
    summary.size  = sizeof(astl_metric_statistics_t);
    summary.flags = ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG;
    REQUIRE(GetMetricStatisticsOnTarget(target_handle, metric_handle.get(), &summary) == ASTL_STATUS_SUCCESS);
    REQUIRE(summary.count == 1);
    REQUIRE(summary.flags == ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG);
    REQUIRE(summary.min.ui64 == 42);
    REQUIRE(summary.max.ui64 == 42);
    // Average is stored as double, rounded to 2 decimal places
    REQUIRE(summary.avg.fp64 == Catch::Approx(42.0).margin(0.01));
  }

  SECTION("Multiple samples - verifies min, max, avg") {
    std::vector<astl::ProcessedSampledData> samples;
    samples.emplace_back(astl::AstlValue{uint64_t{10}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});
    samples.emplace_back(astl::AstlValue{uint64_t{20}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{200}});
    samples.emplace_back(astl::AstlValue{uint64_t{30}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{300}});

    REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

    astl_metric_statistics_t summary{};
    summary.size  = sizeof(astl_metric_statistics_t);
    summary.flags = ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG;
    REQUIRE(GetMetricStatisticsOnTarget(target_handle, metric_handle.get(), &summary) == ASTL_STATUS_SUCCESS);
    REQUIRE(summary.count == 3);
    REQUIRE(summary.flags == ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG);
    REQUIRE(summary.min.ui64 == 10);
    REQUIRE(summary.max.ui64 == 30);
    // (10 + 20 + 30) / 3 = 20.0
    REQUIRE(summary.avg.fp64 == Catch::Approx(20.0).margin(0.01));
  }

  SECTION("Time-weighted average mode") {
    std::vector<astl::ProcessedSampledData> samples;
    samples.emplace_back(astl::AstlValue{uint64_t{10}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});
    samples.emplace_back(astl::AstlValue{uint64_t{20}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{200}});
    samples.emplace_back(astl::AstlValue{uint64_t{30}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{500}});

    REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

    astl_metric_statistics_t summary{};
    summary.size  = sizeof(astl_metric_statistics_t);
    summary.flags = ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG;
    REQUIRE(GetMetricStatisticsOnTarget(target_handle, metric_handle.get(), &summary) == ASTL_STATUS_SUCCESS);
    REQUIRE(summary.count == 3);
    REQUIRE(summary.flags == ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG);
    REQUIRE(summary.min.ui64 == 10);
    REQUIRE(summary.max.ui64 == 30);
    // Left-hold time-weighted average: (10*100 + 20*300) / 400 = 17.5
    REQUIRE(summary.avg.fp64 == Catch::Approx(17.5).margin(0.01));
  }
}

TEST_CASE("astlGetMetricStatisticsOnTarget - float64 samples", "[summary_api]") {
  // --- Set up mock target ---
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"FloatTarget"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  // --- Set up metric ---
  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["FloatTarget"] = {0x1234};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "FloatMetric", "FloatMetric", ASTL_UNITS_NONE, ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  static std::string const metric_name{"FloatMetric"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_FLOAT64;
        _1->metric_type = ASTL_METRIC_VALUE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  auto*                    orchestrator_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(astl::AstlValue{1.5},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});
  samples.emplace_back(astl::AstlValue{3.7},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{200}});
  samples.emplace_back(astl::AstlValue{2.1},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{300}});
  samples.emplace_back(astl::AstlValue{5.9},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{400}});

  REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

  astl_metric_statistics_t summary{};
  summary.size  = sizeof(astl_metric_statistics_t);
  summary.flags = 0;
  REQUIRE(GetMetricStatisticsOnTarget(target_handle, metric_handle.get(), &summary) == ASTL_STATUS_SUCCESS);
  REQUIRE(summary.count == 4);
  REQUIRE(summary.min.fp64 == Catch::Approx(1.5).margin(0.01));
  REQUIRE(summary.max.fp64 == Catch::Approx(5.9).margin(0.01));
  // (1.5 + 3.7 + 2.1 + 5.9) / 4 = 3.3
  REQUIRE(summary.avg.fp64 == Catch::Approx(3.3).margin(0.01));
}

TEST_CASE("astlGetMetricStatisticsOnTarget - unsupported type returns NOT_SUPPORTED", "[summary_api]") {
  // --- Set up mock target ---
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"BoolTarget"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  // --- Set up a metric that reports BOOL8 type (unsupported by MinMaxAvgSummarizer) ---
  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["BoolTarget"] = {0xBEEF};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "BoolMetric", "BoolMetric", ASTL_UNITS_NONE, ASTL_VALUE_BOOL8, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  static std::string const metric_name{"BoolMetric"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_BOOL8;
        _1->metric_type = ASTL_METRIC_VALUE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  auto*                    orchestrator_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Inject a boolean sample so the sample store is not empty
  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(astl::AstlValue{true},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{100}});
  REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

  astl_metric_statistics_t summary{};
  summary.size  = sizeof(astl_metric_statistics_t);
  summary.flags = 0;
  auto result   = GetMetricStatisticsOnTarget(target_handle, metric_handle.get(), &summary);
  REQUIRE(result == ASTL_STATUS_NOT_SUPPORTED);
}

TEST_CASE("astlGetMetricStatisticsOnTarget - invalid target handle", "[summary_api]") {
  // Create a valid orchestrator with no targets so ANY target handle is invalid
  auto mock_metric_manager = std::make_unique<MockMetricManager>();
  auto topology_manager    = std::make_unique<MockTopologyManager>();
  auto collector_manager   = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector_manager, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector_manager, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_manager, RemoveAllMetrics());
  auto output_manager = std::make_unique<astl::OutputManager>();
  auto orchestrator =
      std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                           std::move(mock_metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  int                      dummy_target{};
  int                      dummy_metric{};
  astl_target_handle_t     bad_handle      = static_cast<astl_target_handle_t>(&dummy_target);
  astl_metric_handle_t     non_null_metric = static_cast<astl_metric_handle_t>(&dummy_metric);
  astl_metric_statistics_t summary{};
  summary.size  = sizeof(astl_metric_statistics_t);
  summary.flags = 0;
  auto result   = GetMetricStatisticsOnTarget(bad_handle, non_null_metric, &summary);
  REQUIRE(result == ASTL_STATUS_INVALID_TARGET_HANDLE);
}
// ===========================================================================
//  Discrete Histogram API tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Argument validation (no orchestrator needed)
// ---------------------------------------------------------------------------

TEST_CASE("astlGetMetricDiscreteHistogramBinCountOnTarget - NULL arguments", "[histogram_api]") {
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);
  uint32_t             bin_count{};

  REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(nullptr, non_null_metric, &bin_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(non_null_target, nullptr, &bin_count) == ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(non_null_target, non_null_metric, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetMetricDiscreteHistogramOnTarget - NULL arguments", "[histogram_api]") {
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);

  std::vector<astl_discrete_histogram_bin_t> bins(1);
  bins[0].size   = sizeof(astl_discrete_histogram_bin_t);
  uint32_t count = 1;

  REQUIRE(GetMetricDiscreteHistogramOnTarget(nullptr, non_null_metric, bins.data(), &count) ==
          ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetMetricDiscreteHistogramOnTarget(non_null_target, nullptr, bins.data(), &count) ==
          ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetMetricDiscreteHistogramOnTarget(non_null_target, non_null_metric, nullptr, &count) ==
          ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(GetMetricDiscreteHistogramOnTarget(non_null_target, non_null_metric, bins.data(), nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetMetricDiscreteHistogramOnTarget - zero bin_count", "[histogram_api]") {
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);

  std::vector<astl_discrete_histogram_bin_t> bins(1);
  bins[0].size   = sizeof(astl_discrete_histogram_bin_t);
  uint32_t count = 0;

  REQUIRE(GetMetricDiscreteHistogramOnTarget(non_null_target, non_null_metric, bins.data(), &count) ==
          ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("astlGetMetricDiscreteHistogramOnTarget - invalid struct size", "[histogram_api]") {
  int                  sentinel_target{};
  int                  sentinel_metric{};
  astl_target_handle_t non_null_target = static_cast<astl_target_handle_t>(&sentinel_target);
  astl_metric_handle_t non_null_metric = static_cast<astl_metric_handle_t>(&sentinel_metric);

  std::vector<astl_discrete_histogram_bin_t> bins(1);
  uint32_t                                   count = 1;

  SECTION("Size too small") {
    bins[0].size = sizeof(astl_discrete_histogram_bin_t) - 1;
    REQUIRE(GetMetricDiscreteHistogramOnTarget(non_null_target, non_null_metric, bins.data(), &count) ==
            ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
  }
  SECTION("Size too large") {
    bins[0].size = sizeof(astl_discrete_histogram_bin_t) + 1;
    REQUIRE(GetMetricDiscreteHistogramOnTarget(non_null_target, non_null_metric, bins.data(), &count) ==
            ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE);
  }
}

// ---------------------------------------------------------------------------
// Helper: build a wired-up target+metric+orchestrator for histogram tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// End-to-end histogram tests
// ---------------------------------------------------------------------------

TEST_CASE("astlGetMetricDiscreteHistogramBinCountOnTarget - no samples yields 0", "[histogram_api]") {
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"HistTarget"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["HistTarget"] = {0xABCD};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "HistMetric", "HistMetric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  static std::string const metric_name{"HistMetric"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_UINT64;
        _1->metric_type = ASTL_METRIC_VALUE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t bin_count = 99;
  REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(target_handle, metric_handle.get(), &bin_count) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(bin_count == 0);
}

TEST_CASE("astlGetMetricDiscreteHistogramOnTarget - uint64 samples, correct bins", "[histogram_api]") {
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"HistTarget2"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["HistTarget2"] = {0xABCD};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "HistMetric2", "HistMetric2", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
      astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  static std::string const metric_name{"HistMetric2"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_UINT64;
        _1->metric_type = ASTL_METRIC_VALUE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  auto*                    orchestrator_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  // Inject: values 10 (×3), 20 (×1), 30 (×2)  → 3 unique bins
  std::vector<astl::ProcessedSampledData> samples;
  for (int i = 0; i < 3; ++i) {
    samples.emplace_back(astl::AstlValue{uint64_t{10}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{i * 100}});
  }
  samples.emplace_back(astl::AstlValue{uint64_t{20}},
                       astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{400}});
  for (int i = 0; i < 2; ++i) {
    samples.emplace_back(astl::AstlValue{uint64_t{30}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{500 + (i * 100)}});
  }
  REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

  SECTION("BinCount returns 3") {
    uint32_t bin_count = 0;
    REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(target_handle, metric_handle.get(), &bin_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(bin_count == 3);
  }

  SECTION("GetDiscreteHistogram fills correct values and counts in ascending order") {
    uint32_t                                   bin_count = 3;
    std::vector<astl_discrete_histogram_bin_t> bins(bin_count);
    bins[0].size = sizeof(astl_discrete_histogram_bin_t);

    REQUIRE(GetMetricDiscreteHistogramOnTarget(target_handle, metric_handle.get(), bins.data(), &bin_count) ==
            ASTL_STATUS_SUCCESS);
    REQUIRE(bin_count == 3);
    REQUIRE(bins[0].value.ui64 == 10);
    REQUIRE(bins[0].count == 3);
    REQUIRE(bins[1].value.ui64 == 20);
    REQUIRE(bins[1].count == 1);
    REQUIRE(bins[2].value.ui64 == 30);
    REQUIRE(bins[2].count == 2);
  }

  SECTION("Buffer too small returns TOO_SMALL and updates bin_count to required") {
    uint32_t                                   bin_count = 2;  // too small: 3 bins required
    std::vector<astl_discrete_histogram_bin_t> bins(bin_count);
    bins[0].size = sizeof(astl_discrete_histogram_bin_t);

    auto result = GetMetricDiscreteHistogramOnTarget(target_handle, metric_handle.get(), bins.data(), &bin_count);
    REQUIRE(result == ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL);
    REQUIRE(bin_count == 3);
  }
}

TEST_CASE("astlGetMetricDiscreteHistogramOnTarget - single unique value", "[histogram_api]") {
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"HistTarget3"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["HistTarget3"] = {0xBEEF};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "SingleMetric", "SingleMetric", ASTL_UNITS_NONE, ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
      ASTL_METRIC_VALUE, astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  static std::string const metric_name{"SingleMetric"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_UINT64;
        _1->metric_type = ASTL_METRIC_VALUE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  auto*                    orchestrator_raw = orchestrator.get();
  TestOrchestratorInjector injector(std::move(orchestrator));

  std::vector<astl::ProcessedSampledData> samples;
  for (int i = 0; i < 5; ++i) {
    samples.emplace_back(astl::AstlValue{uint64_t{42}},
                         astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{(i * 100)}});
  }
  REQUIRE(orchestrator_raw->SinkProcessedSamples(mock_target_raw, mock_metric_raw, samples) == ASTL_STATUS_SUCCESS);

  uint32_t bin_count = 0;
  REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(target_handle, metric_handle.get(), &bin_count) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(bin_count == 1);

  astl_discrete_histogram_bin_t bin{};
  bin.size = sizeof(astl_discrete_histogram_bin_t);
  REQUIRE(GetMetricDiscreteHistogramOnTarget(target_handle, metric_handle.get(), &bin, &bin_count) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(bin.value.ui64 == 42);
  REQUIRE(bin.count == 5);
}

TEST_CASE("astlGetMetricDiscreteHistogramOnTarget - unsupported metric type returns NOT_SUPPORTED", "[histogram_api]") {
  // ASTL_METRIC_RATE is not supported by the discrete HistogramSummarizer
  auto                       mock_target     = std::make_unique<MockTarget>();
  auto*                      mock_target_raw = mock_target.get();
  astl_target_handle_t const target_handle   = mock_target_raw;
  static std::string const   target_name{"HistTarget4"};
  ALLOW_CALL(*mock_target, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle = target_handle;
        _1->name   = target_name.c_str();
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_target, Name()).RETURN(target_name);

  astl::ScmiTargetToDataEventIdMap data_event_ids;
  data_event_ids["HistTarget4"] = {0x1234};
  astl::ScmiMultiTargetOperationBuilder op_builder{data_event_ids};
  auto                                  metric_config = std::make_unique<astl::MetricConfig>(
      "RateMetric", "RateMetric", ASTL_UNITS_NONE, ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_RATE,
      astl::CollectorType::SCMI, std::move(op_builder));

  auto                                                                     mock_metric = std::make_unique<MockMetric>();
  auto*                                                                    mock_metric_raw = mock_metric.get();
  std::unordered_map<const astl::ITarget*, std::unique_ptr<astl::IMetric>> target_to_metric;
  target_to_metric[mock_target_raw] = std::move(mock_metric);
  auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(metric_config), std::move(target_to_metric));
  astl_metric_handle_t metric_api_handle = static_cast<astl_metric_handle_t>(metric_handle.get());

  auto  mock_metric_manager = std::make_unique<MockMetricManager>();
  auto* mm_raw              = mock_metric_manager.get();
  ALLOW_CALL(*mm_raw, GetMetricOnTarget(metric_api_handle, mock_target_raw)).RETURN(mock_metric_raw);

  static std::string const metric_name{"RateMetric"};
  ALLOW_CALL(*mock_metric_raw, GetProperties(_))
      .SIDE_EFFECT({
        _1->handle      = metric_api_handle;
        _1->name        = metric_name.c_str();
        _1->value_type  = ASTL_VALUE_FLOAT64;
        _1->metric_type = ASTL_METRIC_RATE;
      })
      .RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*mock_metric_raw, Name()).RETURN(metric_name);

  auto [orchestrator, expectations] = BuildTestOrchestrator(std::move(mock_target), std::move(mock_metric_manager));
  TestOrchestratorInjector injector(std::move(orchestrator));

  uint32_t bin_count = 0;
  REQUIRE(GetMetricDiscreteHistogramBinCountOnTarget(target_handle, metric_handle.get(), &bin_count) ==
          ASTL_STATUS_NOT_SUPPORTED);

  astl_discrete_histogram_bin_t bin{};
  bin.size  = sizeof(astl_discrete_histogram_bin_t);
  bin_count = 1;
  REQUIRE(GetMetricDiscreteHistogramOnTarget(target_handle, metric_handle.get(), &bin, &bin_count) ==
          ASTL_STATUS_NOT_SUPPORTED);
}
