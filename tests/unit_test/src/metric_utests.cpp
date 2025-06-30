#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "metric/sampled_value_metric.hpp"

TEST_CASE("SampledValueMetric: construction & ReceiveSample single sample", "[SampledValueMetric]") {
  // 1) Construct a metric for 64-bit unsigned samples
  astl::SampledValueMetric metric("test_metric", "unit-test metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);
  // 2) Create a sample with a random value.
  // NOLINTNEXTLINE
  astl::AstlValue   val1{uint64_t{40}};
  astl::SampledData sample1(1, val1);
  auto              status1 = metric.ReceiveSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_SUCCESS);
}

TEST_CASE("SampledValueMetric & ReceiveSample with not supported type", "[SampledValueMetric]") {
  // 1) Construct a metric for 32-bit unsigned samples
  astl::SampledValueMetric metric("test_metric", "unit-test metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT32);
  // 2) Create a sample with a random value.
  // NOLINTNEXTLINE
  astl::AstlValue   val1{uint64_t{40}};
  astl::SampledData sample1(1, val1);
  auto              status1 = metric.ReceiveSample(sample1);
  REQUIRE(status1 == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

TEST_CASE("SampledValueMetric: GetSummaryData returns correct summary", "[SampledValueMetric]") {
  // Construct metric and feed samples
  astl::SampledValueMetric metric("test_metric", "unit-test metric", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64);
  // NOLINTNEXTLINE
  std::vector<uint64_t> values = {10, 20, 30, 40};
  for (auto value : values) {
    astl::AstlValue   sample_value{uint64_t{value}};
    astl::SampledData sample(1, sample_value);
    auto              status = metric.ReceiveSample(sample);
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