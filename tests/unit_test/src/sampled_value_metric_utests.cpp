// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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
  auto as_u64 = [](const std::optional<astl::AstlValue> &val) -> uint64_t {
    return std::get<uint64_t>(val.value().value);
  };
  REQUIRE(as_u64(summary.min) == 10);
  REQUIRE(as_u64(summary.max) == 40);
  REQUIRE(as_u64(summary.avg) == 25);
}
