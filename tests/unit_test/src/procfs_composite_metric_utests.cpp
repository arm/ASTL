// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "../../test_includes.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "metric/procfs_composite_metricconfig.hpp"

namespace {

struct RecordingSink : public astl::IProcessedSampleSink {
  std::vector<astl::ProcessedSampledData> received;

  auto SinkProcessedSamples(const astl::ITarget* target, const astl::IMetric* metric,
                            std::span<const astl::ProcessedSampledData> samples) -> astl_status_code override {
    (void)target;
    (void)metric;
    received.insert(received.end(), samples.begin(), samples.end());
    return ASTL_STATUS_SUCCESS;
  }
};

auto MakeMemUsedInputBindings() -> std::vector<astl::ProcfsCompositeMetricConfig::InputBinding> {
  return {
      {"mem_total",     astl::procfs::KeyValueField{"meminfo", "MemTotal", ASTL_VALUE_UINT64}    },
      {"mem_available", astl::procfs::KeyValueField{"meminfo", "MemAvailable", ASTL_VALUE_UINT64}},
  };
}

auto MakeMemUsedConfig() -> astl::ProcfsCompositeMetricConfig {
  return astl::ProcfsCompositeMetricConfig{
      astl::ProcfsCompositeMetricConfig::CreateParams{
                                                      .name              = "meminfo.MemUsed",
                                                      .description       = "derived used memory",
                                                      .units             = ASTL_UNITS_BYTES,
                                                      .value_type        = ASTL_VALUE_UINT64,
                                                      .identifier        = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                                      .metric_type       = ASTL_METRIC_VALUE,
                                                      .inputs            = MakeMemUsedInputBindings(),
                                                      .requires_previous = false,
                                                      .formula_text      = "max(mem_total - mem_available, 0) * 1024",
                                                      .metric_groups     = {},
                                                      .metric_id         = {},
                                                      }
  };
}

auto FeedCompositeBatch(astl::ProcfsCompositeMetric&                                    metric,
                        const std::expected<astl::OperationSequence, astl_status_code>& operations_or_error,
                        const astl::ProcessedSampleTimestamp& timestamp, uint64_t first_value, uint64_t second_value)
    -> astl_status_code {
  const auto first_status = metric.ReceiveRawSample(
      astl::NormalizedSampledData{(*operations_or_error)[0]->GetId(), astl::AstlValue{first_value}, timestamp});
  if (first_status != ASTL_STATUS_SUCCESS) {
    return first_status;
  }
  return metric.ReceiveRawSample(
      astl::NormalizedSampledData{(*operations_or_error)[1]->GetId(), astl::AstlValue{second_value}, timestamp});
}

auto AssertMemUsedCompositeComputation() -> void {
  auto config = MakeMemUsedConfig();

  RecordingSink               sink;
  astl::ProcfsCompositeMetric metric{&config, nullptr, &sink};
  auto                        operations_or_error = metric.GetOperations();
  REQUIRE(operations_or_error.has_value());
  REQUIRE(operations_or_error->size() == 2);

  const auto timestamp = astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{1000}};
  REQUIRE(FeedCompositeBatch(metric, operations_or_error, timestamp, 1024, 256) == ASTL_STATUS_SUCCESS);

  REQUIRE(sink.received.size() == 1);
  REQUIRE(sink.received.front().get<uint64_t>() == uint64_t{786432});
}

}  // namespace

TEST_CASE("ProcfsCompositeMetric computes stateless composites from multiple inputs", "[procfs_composite_metric]") {
  AssertMemUsedCompositeComputation();
}

TEST_CASE("ProcfsCompositeMetric computes delta-based composites after the first sample", "[procfs_composite_metric]") {
  std::vector<astl::ProcfsCompositeMetricConfig::InputBinding> inputs{
      {"total", astl::procfs::KeyValueField{"stat", "Total", ASTL_VALUE_UINT64}},
      {"idle",  astl::procfs::KeyValueField{"stat", "Idle", ASTL_VALUE_UINT64} },
  };

  astl::ProcfsCompositeMetricConfig config{
      astl::ProcfsCompositeMetricConfig::CreateParams{
                                                      .name              = "stat.cpu.utilization",
                                                      .description       = "cpu utilization",
                                                      .units             = ASTL_UNITS_PERCENT,
                                                      .value_type        = ASTL_VALUE_FLOAT64,
                                                      .identifier        = ASTL_METRIC_IDENTIFIER_UNKNOWN,
                                                      .metric_type       = ASTL_METRIC_VALUE,
                                                      .inputs            = std::move(inputs),
                                                      .requires_previous = true,
                                                      .formula_text =
              "clamp(if(delta_total == 0, 0, max(delta_total - delta_idle, 0) * 100 / delta_total), 0, 100)", .metric_groups = {},
                                                      .metric_id     = {},
                                                      }
  };

  RecordingSink               sink;
  astl::ProcfsCompositeMetric metric{&config, nullptr, &sink};
  auto                        operations_or_error = metric.GetOperations();
  REQUIRE(operations_or_error.has_value());
  REQUIRE(operations_or_error->size() == 2);

  const auto timestamp_a = astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{1000}};
  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{
              (*operations_or_error)[0]->GetId(), astl::AstlValue{uint64_t{36}}, timestamp_a}) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{
              (*operations_or_error)[1]->GetId(), astl::AstlValue{uint64_t{9}}, timestamp_a}) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.received.empty());

  const auto timestamp_b = astl::ProcessedSampleTimestamp{astl::ProcessedSampleTimestamp::duration{2000}};
  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{
              (*operations_or_error)[0]->GetId(), astl::AstlValue{uint64_t{46}}, timestamp_b}) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric.ReceiveRawSample(astl::NormalizedSampledData{
              (*operations_or_error)[1]->GetId(), astl::AstlValue{uint64_t{13}}, timestamp_b}) == ASTL_STATUS_SUCCESS);

  REQUIRE(sink.received.size() == 1);
  REQUIRE(sink.received.front().get<double>() == Catch::Approx(60.0));
}

TEST_CASE("ProcfsCompositeMetric supports tinyexpr composite formulas with named variables",
          "[procfs_composite_metric]") {
  AssertMemUsedCompositeComputation();
}
