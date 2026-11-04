// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <barrier>
#include <future>
#include <vector>

#include "../../test_includes.hpp"  // include before catch2
#include "metric/sampled_value_metric.hpp"
#include "operation/operation_builder.hpp"

astl::SampledValueMetric GetSampledValueMetricUINT64() {
  static astl::MetricConfig config{"test_metric",
                                   "unit-test metric",
                                   ASTL_UNITS_CELSIUS,
                                   ASTL_VALUE_UINT64,
                                   ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                   ASTL_METRIC_VALUE,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return astl::SampledValueMetric{&config, nullptr, nullptr};
}
astl::SampledValueMetric GetSampledValueMetricUINT32() {
  static astl::MetricConfig config{"test_metric",
                                   "unit-test metric",
                                   ASTL_UNITS_CELSIUS,
                                   ASTL_VALUE_UINT32,
                                   ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                   ASTL_METRIC_VALUE,
                                   astl::CollectorType::UNKNOWN,
                                   astl::NullOperationBuilder{}};
  return astl::SampledValueMetric{&config, nullptr, nullptr};
}

TEST_CASE("SampledValueMetric: construction & ReceiveRawSample single sample", "[SampledValueMetric]") {
  // 1) Construct a metric for 64-bit unsigned samples
  astl::SampledValueMetric metric = GetSampledValueMetricUINT64();
  // 2) Create a sample with a random value.
  // NOLINTNEXTLINE
  astl::AstlValue             val1{uint64_t{40}};
  astl::NormalizedSampledData sample1(1, val1);
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);
}

TEST_CASE("SampledValueMetric & ReceiveRawSample with not supported type", "[SampledValueMetric]") {
  // 1) Construct a metric for 32-bit unsigned samples
  astl::SampledValueMetric metric = GetSampledValueMetricUINT32();
  // 2) Create a sample with a random value.
  // NOLINTNEXTLINE
  astl::AstlValue             val1{uint64_t{40}};
  astl::NormalizedSampledData sample1(1, val1);
  auto                        status1 = metric.ReceiveRawSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

TEST_CASE("SampledValueMetric: GetSummaryData returns correct summary", "[SampledValueMetric]") {
  // Construct metric and feed samples
  astl::SampledValueMetric metric = GetSampledValueMetricUINT64();
  // NOLINTNEXTLINE
  std::vector<uint64_t> values = {10, 20, 30, 40};
  for (auto value : values) {
    astl::AstlValue             sample_value{uint64_t{value}};
    astl::NormalizedSampledData sample(1, sample_value);
    auto                        status = metric.ReceiveRawSample(sample);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
  }

  // Summarize the metric to compute min, max, and average
  auto status = metric.Summarize();
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  // Retrieve summary
  auto summary = metric.GetSummaryData();
  // Expected: min=10, max=40, avg=(10+20+30+40)/4 = 25
  auto as_u64 = [](const std::optional<astl::AstlValue>& val) -> uint64_t {
    return std::get<uint64_t>(val.value().value);
  };
  REQUIRE(as_u64(summary.min) == 10);
  REQUIRE(as_u64(summary.max) == 40);
  REQUIRE(as_u64(summary.avg) == 25);
}

TEST_CASE("SampledValueMetric: running statistics accumulate and reset starts a clean collection",
          "[SampledValueMetric]") {
  auto metric = GetSampledValueMetricUINT64();
  for (uint64_t value : {uint64_t{10}, uint64_t{30}}) {
    REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{1, astl::AstlValue{value}}) == ASTL_STATUS_SUCCESS);
  }
  REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.GetSummaryData().min == astl::AstlValue{uint64_t{10}});
  REQUIRE(metric.GetSummaryData().max == astl::AstlValue{uint64_t{30}});
  REQUIRE(metric.GetSummaryData().avg == astl::AstlValue{uint64_t{20}});

  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{1, astl::AstlValue{uint64_t{50}}}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.GetSummaryData().avg == astl::AstlValue{uint64_t{30}});

  metric.Reset();
  REQUIRE_FALSE(metric.GetSummaryData().min.has_value());
  REQUIRE_FALSE(metric.GetSummaryData().max.has_value());
  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{1, astl::AstlValue{uint64_t{80}}}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.GetSummaryData().min == astl::AstlValue{uint64_t{80}});
  REQUIRE(metric.GetSummaryData().max == astl::AstlValue{uint64_t{80}});
  REQUIRE(metric.GetSummaryData().avg == astl::AstlValue{uint64_t{80}});
}

TEST_CASE("SampledValueMetric: ingestion, reset, and summary access synchronize state", "[SampledValueMetric]") {
  auto              metric = GetSampledValueMetricUINT64();
  std::atomic<bool> ingestion_failed{false};
  std::barrier      start{3};
  {
    auto producer = std::async(std::launch::async, [&] {
      start.arrive_and_wait();
      for (unsigned sample = 0; sample < 10000; ++sample) {
        if (metric.ReceiveRawSample(astl::NormalizedSampledData{1, astl::AstlValue{uint64_t{7}}}) !=
            ASTL_STATUS_SUCCESS) {
          ingestion_failed.store(true);
        }
      }
    });
    auto resetter = std::async(std::launch::async, [&] {
      start.arrive_and_wait();
      for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        metric.Reset();
      }
    });
    start.arrive_and_wait();
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
      REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);
      const auto summary = metric.GetSummaryData();
      if (summary.min.has_value()) {
        REQUIRE(summary.min == astl::AstlValue{uint64_t{7}});
        REQUIRE(summary.max == astl::AstlValue{uint64_t{7}});
        // A reset and new ingestion can occur between Summarize and GetSummaryData.
        REQUIRE((summary.avg == astl::AstlValue{uint64_t{0}} || summary.avg == astl::AstlValue{uint64_t{7}}));
      } else {
        REQUIRE_FALSE(summary.max.has_value());
        REQUIRE(summary.avg == astl::AstlValue{uint64_t{0}});
      }
    }
    producer.get();
    resetter.get();
  }
  REQUIRE_FALSE(ingestion_failed.load());
  metric.Reset();
  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{1, astl::AstlValue{uint64_t{80}}}) ==
          ASTL_STATUS_SUCCESS);
  REQUIRE(metric.Summarize() == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.GetSummaryData().avg == astl::AstlValue{uint64_t{80}});
}
