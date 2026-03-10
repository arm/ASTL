// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "astl/astl_telemetry.h"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "metric/i_metric.hpp"
#include "metric/metric_manager.hpp"
#include "metric/sampled_value_metric.hpp"
#include "operation/scmi_read_operation.hpp"

using astl::Capabilities;
using astl::CollectorCapability;
using astl::CollectorType;
using astl::IMetric;
using astl::IProcessedSampleSink;
using astl::MetricConfig;
using astl::MetricManager;
using astl::OperationSequence;
using astl::ProcessedSampledData;
using astl::RawSampledData;
using astl::ScmiTargetToDataEventIdMap;
using astl::SystemCapability;

constexpr size_t kJunk = 7;
namespace astl {

astl::ScmiDataEventId GetDataEventId(const astl::ResidencyMetricConfig::StateInfo& state_info) {
  if (const auto* scmi_builder = std::get_if<astl::ScmiOperationBuilder>(&state_info.operation_builder)) {
    return scmi_builder->GetDataEventId();
  }
  return astl::ScmiDataEventId{0xFFFFFFFF};  // invalid id
}

}  // namespace astl

// Dummy metric implementation for testing purposes
// This metric simply collects samples and stores them in a vector.
// It can be configured to return a specific status code when processing samples.
struct TestMetric : public IMetric {
  astl_status_code                  lastStatus      = ASTL_STATUS_SUCCESS;
  astl_status_code                  summarizeStatus = ASTL_STATUS_SUCCESS;
  std::vector<RawSampledData>       received;
  std::vector<ProcessedSampledData> processed;
  IProcessedSampleSink*             sink = nullptr;

  astl_status_code ReceiveRawSample(const RawSampledData& raw_sample) override {
    received.push_back(raw_sample);
    return lastStatus;
  }
  // --- Implement remaining pure-virtuals so TestMetric is concrete ---
  bool CheckCapabilities(const Capabilities& /*caps*/) const override { return true; }

  std::expected<OperationSequence, astl_status_code> GetOperations() override {
    // Return an empty sequence by default
    return OperationSequence{};
  }

  void SetProcessedSampleSink(IProcessedSampleSink* processed_sample_sink) override { sink = processed_sample_sink; };

  astl_status_code SinkProcessedSample(const ProcessedSampledData& processed_sample) override {
    if (sink) {
      processed.push_back(processed_sample);
      ProcessedSampledData sample = processed_sample;
      return sink->SinkProcessedSamples(nullptr, this, {&sample, 1});
    }
    return ASTL_STATUS_BAD_ARGUMENT;
  };

  void Reset() override {
    received.clear();
    processed.clear();
  }

  astl_status_code Summarize() override {
    // No-op summary
    return summarizeStatus;
  }

  astl_status_code GetProperties(astl_metric_properties_t* /*props*/) const override {
    // No special properties
    return ASTL_STATUS_SUCCESS;
  }

  auto Name() const -> std::string const& override {
    const static std::string name{"TestMetric"};
    return name;
  }
};

static Capabilities MakeCaps(CollectorType collector_type) {
  // Build a Capabilities object with exactly one collector type.
  // We don’t use the SystemCapability in these tests
  std::vector<CollectorCapability> col_caps{CollectorCapability{collector_type}};
  std::vector<SystemCapability>    sys_caps{SystemCapability{}};
  return Capabilities{std::move(col_caps), std::move(sys_caps)};
}

TEST_CASE("MetricManager::RegisterMetric succeeds when collector supported", "[MetricManager]") {
  // 1) Register a single SCMI metric with data_event_id "0x123"
  // 2) Retrieve available metrics via GetAvailableMetrics
  // 3) Fetch required operations and verify we get exactly one ScmiReadOperation with ID==0x123
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // 2) Build a MetricConfig whose collector type is SCMI
  auto cfg =
      std::make_unique<MetricConfig>("metricA",                             // name
                                     "descr",                               // description
                                     astl_units_t::ASTL_UNITS_CELSIUS,      // units
                                     astl_value_type_t::ASTL_VALUE_UINT64,  // value type
                                     ASTL_CATEGORY_UNCATEGORIZED,  // category (tests don't depend on category yet)
                                     astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                     CollectorType::SCMI,                    // collector type
                                     astl::NullOperationBuilder{}            // operation builder
      );

  MockTarget  target;
  std::string target_name{"AP0"};
  ALLOW_CALL(target, Name()).RETURN(target_name);
  astl_status_code status = mgr.RegisterMetric(std::move(cfg), {&target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.GetAvailableMetrics(&target).value().size() == 1);
}

TEST_CASE("MetricManager::RegisterMetric fails when collector unsupported", "[MetricManager]") {
  // 1) Manager only supports SCMI
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // 2) Build a MetricConfig whose collector type is MMIO - Not supported
  auto cfg = std::make_unique<MetricConfig>("metricB", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                            astl_value_type_t::ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                            astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::MMIO,
                                            astl::NullOperationBuilder{});

  astl_status_code status = mgr.RegisterMetric(std::move(cfg), {});
  REQUIRE(status == ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE);
}

TEST_CASE("MetricManager::GetRequiredOperations succeeds with valid SCMI metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"AP0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  astl::ScmiOperationBuilder op_builder{0x123};
  auto                       cfg = std::make_unique<MetricConfig>(
      "metricA", "descr", astl_units_t::ASTL_UNITS_CELSIUS, astl_value_type_t::ASTL_VALUE_UINT64,
      ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI, std::move(op_builder));

  REQUIRE(mgr.RegisterMetric(std::move(cfg), {target.get()}) == ASTL_STATUS_SUCCESS);
  // Retrieve the registered metrics
  auto avail_or_error = mgr.GetAvailableMetrics(target.get());
  REQUIRE(avail_or_error.has_value());
  auto metrics = *avail_or_error;
  REQUIRE(metrics.size() == 1);

  // Obtain the required SCMI operations
  auto const& ops = mgr.GetRequiredOperations(metrics, target.get());
  REQUIRE(ops);
  REQUIRE_FALSE(ops->operationsOnSample.empty());
  // Verify there is exactly one operation with ID 0x123
  astl::OperationSequence const& op_seq = ops->operationsOnSample;
  REQUIRE(op_seq.size() == 1);
  const auto& base_op = op_seq.front();
  const auto* scmi_op = dynamic_cast<astl::ScmiReadOperation*>(base_op.get());
  REQUIRE(scmi_op != nullptr);
  REQUIRE(scmi_op->scmi_data_event_id == 0x123);
}

TEST_CASE("MetricManager::GetRequiredOperations fails for unregistered metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;

  // Metric pointer not registered
  auto                                      junkval{kJunk};
  const auto* const                         unregistered_metric = static_cast<astl_metric_handle_t>(&junkval);
  std::array<const astl_metric_handle_t, 1> metrics_array{unregistered_metric};
  std::span<const astl_metric_handle_t>     metric_span(metrics_array);
  auto                                      result = mgr.GetRequiredOperations(metric_span, &target);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_INVALID_METRIC_HANDLE);
}

TEST_CASE("MetricManager::GetRequiredOperations fails for non-SCMI metric", "[MetricManager]") {
  Capabilities                   caps = MakeCaps(CollectorType::SCMI);
  MetricManager                  mgr(caps);
  MockTarget                     target;
  astl::MetricConfig             config{"metricA",
                            "descr",
                            astl_units_t::ASTL_UNITS_CELSIUS,
                            astl_value_type_t::ASTL_VALUE_UINT64,
                            ASTL_CATEGORY_UNCATEGORIZED,
                            astl_metric_type_t::ASTL_METRIC_VALUE,
                            CollectorType::MMIO,  // MMIO not supported by manager
                            astl::NullOperationBuilder{}};
  std::unique_ptr<astl::IMetric> owner_metric_mmio =
      std::make_unique<astl::SampledValueMetric>(&config, &target, nullptr);

  // Manually associate the metric pointer
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric_mmio),
      std::make_unique<MetricConfig>("metricC", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                     astl_value_type_t::ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::MMIO,
                                     astl::ScmiOperationBuilder{0x123}),
      &target);

  // Retrieve the metric.
  auto avail_or_error = mgr.GetAvailableMetrics(&target);
  REQUIRE(avail_or_error.has_value());
  auto metric_span = *avail_or_error;
  auto result      = mgr.GetRequiredOperations(metric_span, &target);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE);
}

TEST_CASE("MetricManager::GetRequiredOperations correctly discriminates metrics by target", "[MetricManager]") {
  // In this test we'll ensure that metricManager can properly route
  // metrics_handles to their target-specific metric instance.

  // Create 2 mock targets
  auto        target0 = std::make_unique<MockTarget>();
  std::string target0_name{"AP0"};
  ALLOW_CALL(*target0, Name()).RETURN(target0_name);
  auto        target1 = std::make_unique<MockTarget>();
  std::string target1_name{"AP1"};
  ALLOW_CALL(*target1, Name()).RETURN(target1_name);

  // Create 3 metric configs. One for each target, and one that can run on either target
  astl::ScmiMultiTargetOperationBuilder t0_builder{ScmiTargetToDataEventIdMap{{target0_name, {0x123}}}};
  auto                                  cfg0 = std::make_unique<MetricConfig>(
      "metric0", "description0", astl_units_t::ASTL_UNITS_CELSIUS, astl_value_type_t::ASTL_VALUE_UINT64,
      ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI, std::move(t0_builder));

  astl::ScmiMultiTargetOperationBuilder t1_builder{ScmiTargetToDataEventIdMap{{target1_name, {0x456}}}};
  auto                                  cfg1 = std::make_unique<MetricConfig>(
      "metric1", "description1", astl_units_t::ASTL_UNITS_WATTS, astl_value_type_t::ASTL_VALUE_UINT64,
      ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI, std::move(t1_builder));

  // metric01 can be collected on either target 0 or 1
  astl::ScmiMultiTargetOperationBuilder t0_t1_builder{
      ScmiTargetToDataEventIdMap{{target0_name, {0xACED}}, {target1_name, {0xBABE}}}
  };
  auto cfg01 = std::make_unique<MetricConfig>("metric01", "description1", astl_units_t::ASTL_UNITS_AMPS,
                                              astl_value_type_t::ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED,
                                              astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                              std::move(t0_t1_builder));

  // Create a Metric Manager
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  REQUIRE(mgr.RegisterMetric(std::move(cfg0), {target0.get()}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterMetric(std::move(cfg1), {target1.get()}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterMetric(std::move(cfg01), {target0.get(), target1.get()}) == ASTL_STATUS_SUCCESS);

  // Retrieve the registered metrics
  auto avail_or_error = mgr.GetAvailableMetrics(target0.get());
  REQUIRE(avail_or_error.has_value());
  auto metrics = *avail_or_error;
  // cfg0 + cfg01 (is one metric that can run on 2 targets: that still only counts as one.)
  REQUIRE(metrics.size() == 2);

  SECTION("MetricManager::GetRequiredOperations for 1 metric on multiple targets") {
    auto        multi_target_metric = metrics.subspan(1, 1);  // get the metric for cfg01
    auto const& ops_target0         = mgr.GetRequiredOperations(multi_target_metric, target0.get());
    REQUIRE(ops_target0);
    astl::OperationSequence const& op_seq0 = ops_target0->operationsOnSample;
    REQUIRE(op_seq0.size() == 1);  // the DE ID for target0
    const auto& base_op0 = op_seq0.front();
    const auto* scmi_op0 = dynamic_cast<astl::ScmiReadOperation*>(base_op0.get());
    REQUIRE(scmi_op0 != nullptr);
    REQUIRE(scmi_op0->scmi_data_event_id == 0xACED);

    auto const& ops_target1 = mgr.GetRequiredOperations(multi_target_metric, target1.get());
    REQUIRE(ops_target1);
    astl::OperationSequence const& op_seq1 = ops_target1->operationsOnSample;
    REQUIRE(op_seq1.size() == 1);  // the DE ID for target1
    const auto& base_op1 = op_seq1.front();
    const auto* scmi_op1 = dynamic_cast<astl::ScmiReadOperation*>(base_op1.get());
    REQUIRE(scmi_op1 != nullptr);
    REQUIRE(scmi_op1->scmi_data_event_id == 0xBABE);

    REQUIRE(base_op0->GetId() != base_op1->GetId());
  }

  SECTION("MetricManager::GetRequiredOperations for metric on unsupported target") {
    auto        metric0     = metrics.subspan(0, 1);  // get the metric for cfg0
    auto const& ops_target1 = mgr.GetRequiredOperations(metric0, target1.get());
    REQUIRE(!ops_target1);
    REQUIRE(((ops_target1.error() == ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET) ||
             (ops_target1.error() == ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE)));
  }
}

TEST_CASE("MetricManager::ProcessData processes valid sample and returns success", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;

  auto              owner_metric = std::make_unique<TestMetric>();
  TestMetric*       metric_ptr   = owner_metric.get();
  astl::OperationId op_id        = 7;  // Example operation ID
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric),
      std::make_unique<MetricConfig>("test", "desc", astl_units_t::ASTL_UNITS_NONE,
                                     astl_value_type_t::ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     astl::NullOperationBuilder{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, op_id, metric_ptr);

  astl::AstlValue             val1{uint64_t{256}};  // Sample value
  astl::RawSampledData        sample1(op_id, val1);
  std::vector<RawSampledData> samples{sample1};
  astl::RawSamplesMap         samples_map;
  samples_map[&target] = samples;
  REQUIRE(mgr.ProcessRawSamples(samples_map) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric_ptr->received.size() == 1);
  REQUIRE(metric_ptr->received[0].get<uint64_t>() == 256);
}

TEST_CASE("MetricManager::ProcessData processes multiple samples for the same metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;

  auto              owner_metric = std::make_unique<TestMetric>();
  TestMetric*       metric_ptr   = owner_metric.get();
  astl::OperationId op_id        = 9;
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric),
      std::make_unique<MetricConfig>("multi", "desc", astl_units_t::ASTL_UNITS_NONE,
                                     astl_value_type_t::ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     astl::NullOperationBuilder{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, op_id, metric_ptr);

  astl::AstlValue                   val1{uint64_t{100}};
  astl::AstlValue                   val2{uint64_t{200}};
  astl::RawSampledData              sample1(op_id, val1);
  astl::RawSampledData              sample2(op_id, val2);
  std::vector<astl::RawSampledData> samples{sample1, sample2};
  astl::RawSamplesMap               samples_map;
  samples_map[&target] = samples;

  REQUIRE(mgr.ProcessRawSamples(samples_map) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric_ptr->received.size() == 2);
  REQUIRE(metric_ptr->received[0].get<uint64_t>() == 100);
  REQUIRE(metric_ptr->received[1].get<uint64_t>() == 200);
}

TEST_CASE("MetricManager::ProcessData processes different metrics for different operations", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;

  auto        owner_metric1 = std::make_unique<TestMetric>();
  auto        owner_metric2 = std::make_unique<TestMetric>();
  TestMetric* metric_ptr1   = owner_metric1.get();
  TestMetric* metric_ptr2   = owner_metric2.get();
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric1),
      std::make_unique<MetricConfig>("m1", "desc", astl_units_t::ASTL_UNITS_NONE, astl_value_type_t::ASTL_VALUE_UINT64,
                                     ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, astl::NullOperationBuilder{}),
      &target);
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric2),
      std::make_unique<MetricConfig>("m2", "desc", astl_units_t::ASTL_UNITS_NONE, astl_value_type_t::ASTL_VALUE_UINT64,
                                     ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, astl::NullOperationBuilder{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 1, metric_ptr1);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 2, metric_ptr2);

  astl::AstlValue                   val1{uint64_t{5}};
  astl::AstlValue                   val2{uint64_t{7}};
  astl::RawSampledData              sample1(1, val1);
  astl::RawSampledData              sample2(2, val2);
  astl::RawSampledData              sample3(1, val1);
  astl::RawSampledData              sample4(2, val2);
  std::vector<astl::RawSampledData> samples{sample1, sample2, sample3, sample4};
  astl::RawSamplesMap               samples_map;
  samples_map[&target] = samples;

  REQUIRE(mgr.ProcessRawSamples(samples_map) == ASTL_STATUS_SUCCESS);
  REQUIRE(metric_ptr1->received.size() == 2);
  REQUIRE(metric_ptr2->received.size() == 2);
}

TEST_CASE("MetricManager::ProcessData stops on error and does not process further samples", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;

  auto owner_metric1        = std::make_unique<TestMetric>();
  auto owner_metric2        = std::make_unique<TestMetric>();
  owner_metric2->lastStatus = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  TestMetric* metric_ptr1   = owner_metric1.get();
  TestMetric* metric_ptr2   = owner_metric2.get();
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric1),
      std::make_unique<MetricConfig>("m1", "desc", astl_units_t::ASTL_UNITS_NONE, astl_value_type_t::ASTL_VALUE_UINT64,
                                     ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, astl::NullOperationBuilder{}),
      &target);
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric2),
      std::make_unique<MetricConfig>("m2", "desc", astl_units_t::ASTL_UNITS_NONE, astl_value_type_t::ASTL_VALUE_UINT64,
                                     ASTL_CATEGORY_UNCATEGORIZED, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, astl::NullOperationBuilder{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 1, metric_ptr1);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 2, metric_ptr2);

  astl::AstlValue                   val1{uint64_t{1}};
  astl::AstlValue                   val2{uint64_t{2}};
  astl::RawSampledData              sample1(1, val1);
  astl::RawSampledData              sample2(2, val2);
  astl::RawSampledData              sample3(1, val1);
  std::vector<astl::RawSampledData> samples{sample1, sample2, sample3};
  astl::RawSamplesMap               samples_map;
  samples_map[&target] = samples;
  REQUIRE(mgr.ProcessRawSamples(samples_map) == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  REQUIRE(metric_ptr1->received.size() == 1);
  REQUIRE(metric_ptr2->received.size() == 1);
}

TEST_CASE("MetricManager::SummarizeMetrics returns success for a TestMetric", "[MetricManager]") {
  // Arrange
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;
  auto          owner_metric = std::make_unique<TestMetric>();
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric),
      std::make_unique<MetricConfig>("metricX", "descX", astl_units_t::ASTL_UNITS_NONE,
                                     astl_value_type_t::ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     astl::NullOperationBuilder{}),
      &target);

  REQUIRE(mgr.SummarizeMetrics() == ASTL_STATUS_SUCCESS);
}

TEST_CASE("MetricManager::SummarizeMetrics returns error for a TestMetric", "[MetricManager]") {
  // Arrange
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;
  std::string   target_name{"AP0"};
  ALLOW_CALL(target, Name()).RETURN(target_name);
  auto owner_metric             = std::make_unique<TestMetric>();
  owner_metric->summarizeStatus = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric),
      std::make_unique<MetricConfig>("metric", "desc", astl_units_t::ASTL_UNITS_NONE,
                                     astl_value_type_t::ASTL_VALUE_UINT64, ASTL_CATEGORY_UNCATEGORIZED,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     astl::NullOperationBuilder{}),
      &target);

  REQUIRE(mgr.SummarizeMetrics() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

TEST_CASE("MetricManager::RegisterMetric succeeds with ResidencyMetricConfig", "[MetricManager][Residency]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  auto        target = std::make_unique<MockTarget>();
  std::string target_name{"AP0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  // Create state info for residency metric
  astl::ResidencyMetricConfig::StateToInfoMap state_info;
  state_info["C1"] = {"C1", "CPU clock-gated idle state", 100000.0,
                      astl::ScmiOperationBuilder{0x1001}};  // C1 state with 100kHz tick frequency
  state_info["C6"] = {"C6", "CPU deep sleep state", 50000.0,
                      astl::ScmiOperationBuilder{0x1002}};  // C6 state with 50kHz tick frequency

  auto residency_config = std::make_unique<astl::ResidencyMetricConfig>(
      "CPU_RESIDENCY", "CPU residency metric", astl_units_t::ASTL_UNITS_SECONDS, astl_value_type_t::ASTL_VALUE_FLOAT64,
      astl_metric_type_t::ASTL_METRIC_RESIDENCY, ASTL_CATEGORY_UNCATEGORIZED, CollectorType::SCMI,
      std::move(state_info), astl::ResidencyMetricConfig::InferredStateInfo{"ACTIVE", "CPU active state"});

  astl_status_code status = mgr.RegisterMetric(std::move(residency_config), {target.get()});
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  // Verify metric was registered
  auto avail_or_error = mgr.GetAvailableMetrics(target.get());
  REQUIRE(avail_or_error.has_value());
  auto metrics = *avail_or_error;
  REQUIRE(metrics.size() == 1);
}

TEST_CASE("MetricManager::GetRequiredOperations with ResidencyMetricConfig creates multiple operations",
          "[MetricManager][Residency]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  auto        target = std::make_unique<MockTarget>();
  std::string target_name{"AP0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  // Create state info for residency metric with multiple states
  astl::ResidencyMetricConfig::StateToInfoMap state_info;
  state_info["C1"] = {"C1", "CPU clock-gated idle state", 100000.0, astl::ScmiOperationBuilder{0x2001}};
  state_info["C6"] = {"C6", "CPU deep sleep state", 50000.0, astl::ScmiOperationBuilder{0x2002}};
  state_info["C7"] = {"C7", "CPU ultra-low power state", 25000.0, astl::ScmiOperationBuilder{0x2003}};

  auto residency_config = std::make_unique<astl::ResidencyMetricConfig>(
      "CPU_RESIDENCY", "CPU residency metric", astl_units_t::ASTL_UNITS_SECONDS, astl_value_type_t::ASTL_VALUE_FLOAT64,
      astl_metric_type_t::ASTL_METRIC_RESIDENCY, ASTL_CATEGORY_UNCATEGORIZED, CollectorType::SCMI,
      std::move(state_info), astl::ResidencyMetricConfig::InferredStateInfo{"ACTIVE", "CPU active state"});

  REQUIRE(mgr.RegisterMetric(std::move(residency_config), {target.get()}) == ASTL_STATUS_SUCCESS);

  auto avail_or_error = mgr.GetAvailableMetrics(target.get());
  REQUIRE(avail_or_error.has_value());
  auto metrics = *avail_or_error;

  // Get required operations - should create one operation per state
  auto ops_result = mgr.GetRequiredOperations(metrics, target.get());
  REQUIRE(ops_result.has_value());

  const auto& operations = ops_result->operationsOnSample;
  REQUIRE(operations.size() == 3);  // One operation for each state (C1, C6, C7)

  // Verify that operations have the correct event IDs
  std::set<uint32_t> expected_event_ids = {0x2001, 0x2002, 0x2003};
  std::set<uint32_t> actual_event_ids;

  for (const auto& operation : operations) {
    const auto* scmi_op = dynamic_cast<const astl::ScmiReadOperation*>(operation.get());
    REQUIRE(scmi_op != nullptr);
    actual_event_ids.insert(scmi_op->scmi_data_event_id);
  }

  REQUIRE(actual_event_ids == expected_event_ids);
}

TEST_CASE("MetricManager::RegisterMetric fails with empty state info for ResidencyMetricConfig",
          "[MetricManager][Residency]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  auto        target = std::make_unique<MockTarget>();
  std::string target_name{"AP0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  // Create residency config with empty state info
  astl::ResidencyMetricConfig::StateToInfoMap empty_state_info;

  auto residency_config = std::make_unique<astl::ResidencyMetricConfig>(
      "EMPTY_RESIDENCY", "Residency metric with no states", astl_units_t::ASTL_UNITS_SECONDS,
      astl_value_type_t::ASTL_VALUE_FLOAT64, astl_metric_type_t::ASTL_METRIC_RESIDENCY, ASTL_CATEGORY_UNCATEGORIZED,
      CollectorType::SCMI, std::move(empty_state_info),
      astl::ResidencyMetricConfig::InferredStateInfo{"ACTIVE", "CPU active state"});

  astl_status_code status = mgr.RegisterMetric(std::move(residency_config), {target.get()});
  REQUIRE(status == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("MetricManager::ResidencyMetricConfig GetStateInfo accessor works correctly", "[MetricManager][Residency]") {
  auto        target = std::make_unique<MockTarget>();
  std::string target_name{"AP0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  // Create state info
  astl::ResidencyMetricConfig::StateToInfoMap state_info;
  state_info["IDLE"]  = {"IDLE", "CPU idle state", 75000.0, astl::ScmiOperationBuilder{0x5001}};
  state_info["SLEEP"] = {"SLEEP", "CPU low-power sleep", 25000.0, astl::ScmiOperationBuilder{0x5002}};

  auto residency_config = std::make_unique<astl::ResidencyMetricConfig>(
      "ACCESSOR_TEST_RESIDENCY", "Test state info accessor", astl_units_t::ASTL_UNITS_SECONDS,
      astl_value_type_t::ASTL_VALUE_FLOAT64, astl_metric_type_t::ASTL_METRIC_RESIDENCY, ASTL_CATEGORY_UNCATEGORIZED,
      CollectorType::SCMI, std::move(state_info),
      astl::ResidencyMetricConfig::InferredStateInfo{"RUNNING", "CPU running state"});

  // Test GetStateInfo() accessor
  const auto& metric_state_info = residency_config->GetStateInfo();

  REQUIRE(metric_state_info.size() == 2);

  REQUIRE(metric_state_info.contains("IDLE"));
  REQUIRE(metric_state_info.contains("SLEEP"));

  const auto& idle_info = metric_state_info.at("IDLE");
  REQUIRE(idle_info.state_name == "IDLE");
  REQUIRE(idle_info.state_description == "CPU idle state");
  REQUIRE(GetDataEventId(idle_info) == 0x5001);
  REQUIRE(idle_info.tick_frequency == 75000.0);

  const auto& sleep_info = metric_state_info.at("SLEEP");
  REQUIRE(sleep_info.state_name == "SLEEP");
  REQUIRE(sleep_info.state_description == "CPU low-power sleep");
  REQUIRE(GetDataEventId(sleep_info) == 0x5002);
  REQUIRE(sleep_info.tick_frequency == 25000.0);

  // Test InferredState() accessor
  REQUIRE(residency_config->InferredState().has_value());
  REQUIRE(residency_config->InferredState()->name == "RUNNING");
  REQUIRE(residency_config->InferredState()->description == "CPU running state");
}

TEST_CASE("MetricManager::GetCounterOnTarget with null args", "[MetricManager][Counter]") {
  MetricManager mgr{MakeCaps(CollectorType::SCMI)};
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"TLM-0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  // Create and register a counter metric
  auto counter_config = std::make_unique<astl::MetricConfig>(
      "TestCounter", "A test counter metric", astl_units_t::ASTL_UNITS_BYTES, astl_value_type_t::ASTL_VALUE_UINT64,
      ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, CollectorType::SCMI, astl::ScmiOperationBuilder{0x6001});
  REQUIRE(mgr.RegisterCounter(std::move(counter_config), {target.get()}) == ASTL_STATUS_SUCCESS);

  auto avail_or_error = mgr.GetAvailableCounters(target.get());
  REQUIRE(avail_or_error.has_value());
  auto counters = *avail_or_error;
  REQUIRE(counters.size() == 1);
  // Get the counter handle
  const auto* counter_handle = counters[0];

  auto result = mgr.GetCounterOnTarget(nullptr, nullptr);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);

  result = mgr.GetCounterOnTarget(counter_handle, nullptr);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET);
}

TEST_CASE("MetricManager::GetCounterOnTarget with unregistered counter", "[MetricManager][Counter]") {
  MetricManager mgr{MakeCaps(CollectorType::SCMI)};
  MockTarget    target;
  ALLOW_CALL(target, Name()).RETURN("TLM-0");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* junk_counter = astl_counter_handle_t{reinterpret_cast<void*>(0x1234)};
  auto        result       = mgr.GetCounterOnTarget(junk_counter, &target);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("MetricManager::GetCounterOnTarget with registered counter", "[MetricManager][Counter]") {
  MetricManager mgr{MakeCaps(CollectorType::SCMI)};
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"TLM-0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  // Create and register a counter metric
  auto counter_config = std::make_unique<astl::MetricConfig>(
      "TestCounter", "A test counter metric", astl_units_t::ASTL_UNITS_BYTES, astl_value_type_t::ASTL_VALUE_UINT64,
      ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, CollectorType::SCMI, astl::ScmiOperationBuilder{0x6001});

  REQUIRE(mgr.RegisterCounter(std::move(counter_config), {target.get()}) == ASTL_STATUS_SUCCESS);

  // Retrieve the registered counters
  size_t num_counters = mgr.GetNumAvailableCounters(target.get());
  REQUIRE(num_counters == 1);

  auto avail_or_error = mgr.GetAvailableCounters(target.get());
  REQUIRE(avail_or_error.has_value());
  auto counters = *avail_or_error;
  REQUIRE(counters.size() == 1);

  // Get the counter handle
  const auto*           counter_handle     = counters[0];
  astl_counter_handle_t counter_api_handle = counter_handle;

  // Call GetCounterOnTarget
  auto result = mgr.GetCounterOnTarget(counter_api_handle, target.get());
  REQUIRE(result.has_value());

  SECTION("GetCounterProperties returns correct properties") {
    astl_counter_properties_t props{};
    auto                      status = mgr.GetCounterProperties(counter_api_handle, &props);
    REQUIRE(status == ASTL_STATUS_SUCCESS);
    REQUIRE(std::string(props._name) == "TestCounter");
    REQUIRE(std::string(props._description) == "A test counter metric");
    REQUIRE(props._units == astl_units_t::ASTL_UNITS_BYTES);
    REQUIRE(std::string(props._formula) == "value");
  }

  SECTION("GetCounterRequiredOperations returns correct operation") {
    std::vector<astl_counter_handle_t> counter_handles_vec{counter_api_handle};
    auto                               ops_result = mgr.GetCounterRequiredOperations(counter_handles_vec, target.get());
    REQUIRE(ops_result.has_value());

    const auto& operations = ops_result->operationsOnSample;
    REQUIRE(operations.size() == 1);  // One operation for the counter

    const auto* scmi_op = dynamic_cast<const astl::ScmiReadOperation*>(operations.front().get());
    REQUIRE(scmi_op != nullptr);
    REQUIRE(scmi_op->scmi_data_event_id == 0x6001);
  }
}

TEST_CASE("MetricManager::GetCounterProperties exposes scaling formula", "[MetricManager][Counter]") {
  MetricManager mgr{MakeCaps(CollectorType::SCMI)};
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"TLM-0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  auto counter_config = std::make_unique<astl::MetricConfig>(
      "ScaledCounter", "Counter with scaling", astl_units_t::ASTL_UNITS_WATTS, astl_value_type_t::ASTL_VALUE_FLOAT64,
      ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, CollectorType::SCMI,
      astl::ScmiOperationBuilder{
          0x6002
  },
      astl::AnyFormula{astl::ScalingFormula{1, 1000}}, astl_value_type_t::ASTL_VALUE_UINT64);

  REQUIRE(mgr.RegisterCounter(std::move(counter_config), {target.get()}) == ASTL_STATUS_SUCCESS);

  auto counters_or_error = mgr.GetAvailableCounters(target.get());
  REQUIRE(counters_or_error.has_value());
  REQUIRE(counters_or_error->size() == 1);
  auto counter_api_handle = astl_counter_handle_t{(*counters_or_error)[0]};

  astl_counter_properties_t props{};
  auto                      status = mgr.GetCounterProperties(counter_api_handle, &props);
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string(props._formula) == "value / 1000");
  REQUIRE(props._value_type == ASTL_VALUE_UINT64);
}

TEST_CASE("MetricManager::GetCounterProperties exposes composed expression and scaling", "[MetricManager][Counter]") {
  MetricManager mgr{MakeCaps(CollectorType::SCMI)};
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"TLM-0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  auto expr_result = astl::ExpressionFormula::Create("value + 1");
  REQUIRE(expr_result.has_value());
  auto composed = astl::ComposeFormulas(
      astl::AnyFormula{
          std::move(expr_result.value())
  },
      astl::AnyFormula{astl::ScalingFormula{1, 1000}});

  auto counter_config = std::make_unique<astl::MetricConfig>(
      "ComposedCounter", "Counter with expression and scaling", astl_units_t::ASTL_UNITS_WATTS,
      astl_value_type_t::ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, CollectorType::SCMI,
      astl::ScmiOperationBuilder{0x6003}, std::move(composed), astl_value_type_t::ASTL_VALUE_UINT64);

  REQUIRE(mgr.RegisterCounter(std::move(counter_config), {target.get()}) == ASTL_STATUS_SUCCESS);

  auto counters_or_error = mgr.GetAvailableCounters(target.get());
  REQUIRE(counters_or_error.has_value());
  REQUIRE(counters_or_error->size() == 1);
  auto counter_api_handle = astl_counter_handle_t{(*counters_or_error)[0]};

  astl_counter_properties_t props{};
  auto                      status = mgr.GetCounterProperties(counter_api_handle, &props);
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string(props._formula).find("/ 1000") != std::string::npos);
  REQUIRE(props._value_type == ASTL_VALUE_UINT64);
  auto api_formula = astl::ExpressionFormula::Create(std::string{props._formula});
  REQUIRE(api_formula.has_value());
  auto api_result = api_formula->Apply(astl::AstlValue{uint64_t{1000}});
  REQUIRE(api_result.has_value());
  REQUIRE(astl::to_string(*api_result) == "1");
}

TEST_CASE("MetricManager::GetCounterProperties keeps integer-literal scaling in fallback pipeline rendering",
          "[MetricManager][Counter]") {
  MetricManager mgr{MakeCaps(CollectorType::SCMI)};
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"TLM-0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  auto expr_a = astl::ExpressionFormula::Create("value + 1");
  auto expr_b = astl::ExpressionFormula::Create("value + 2");
  REQUIRE(expr_a.has_value());
  REQUIRE(expr_b.has_value());

  std::vector<astl::FormulaPipeline::PipelineStep> steps;
  steps.emplace_back(std::move(expr_a.value()));
  steps.emplace_back(astl::ScalingFormula{1, 1000});
  steps.emplace_back(std::move(expr_b.value()));
  astl::AnyFormula pipeline_formula{astl::FormulaPipeline{std::move(steps)}};
  auto             expected_result = astl::ApplyFormula(pipeline_formula, astl::AstlValue{uint64_t{1000}});
  REQUIRE(expected_result.has_value());

  auto counter_config = std::make_unique<astl::MetricConfig>(
      "FallbackCounter", "Counter with multi-step pipeline", astl_units_t::ASTL_UNITS_WATTS,
      astl_value_type_t::ASTL_VALUE_FLOAT64, ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, CollectorType::SCMI,
      astl::ScmiOperationBuilder{0x6004}, std::move(pipeline_formula), astl_value_type_t::ASTL_VALUE_UINT64);

  REQUIRE(mgr.RegisterCounter(std::move(counter_config), {target.get()}) == ASTL_STATUS_SUCCESS);

  auto counters_or_error = mgr.GetAvailableCounters(target.get());
  REQUIRE(counters_or_error.has_value());
  REQUIRE(counters_or_error->size() == 1);
  auto counter_api_handle = astl_counter_handle_t{(*counters_or_error)[0]};

  astl_counter_properties_t props{};
  auto                      status = mgr.GetCounterProperties(counter_api_handle, &props);
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string(props._formula).find("->") == std::string::npos);
  REQUIRE(std::string(props._formula).find("0.001") == std::string::npos);
  REQUIRE(props._value_type == ASTL_VALUE_UINT64);

  // End-to-end lock: emitted API formula must parse in TinyExpr and evaluate the same as metric post-processing.
  auto api_formula = astl::ExpressionFormula::Create(std::string{props._formula});
  REQUIRE(api_formula.has_value());
  auto api_result = api_formula->Apply(astl::AstlValue{uint64_t{1000}});
  REQUIRE(api_result.has_value());
  REQUIRE(astl::to_string(*api_result) == astl::to_string(*expected_result));
}

TEST_CASE("MetricManager::RegisterMetric with no metrics means no groups!", "[MetricManager][MetricGroup]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  MockTarget    target;
  std::string   target_name{"AP0"};
  ALLOW_CALL(target, Name()).RETURN(target_name);

  const auto& available_metrics = mgr.GetAvailableMetrics(&target);
  if (available_metrics) {
    REQUIRE(available_metrics->empty());
  } else {
    REQUIRE(available_metrics.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  const auto& available_groups = mgr.GetMetricGroups(&target);
  if (available_groups) {
    REQUIRE(available_groups->empty());
  } else {
    REQUIRE(available_groups.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("MetricManager::RegisterMetric correctly identifies a metric group with one metric.",
          "[MetricManager][MetricGroup]") {
  // 1) Register a single SCMI metric with data_event_id "0x123"
  // 2) Retrieve available metrics via GetAvailableMetrics()
  // 3) Get metric groups out of MetricManager and verify we have exactly one group with one metric in it.
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  std::vector<std::string> groups{"thermals"};
  // 2) Build a MetricConfig whose collector type is SCMI, belonging to group "thermals"
  auto cfg_a = std::make_unique<MetricConfig>("metricA",                              // name
                                              "descr",                                // description
                                              astl_units_t::ASTL_UNITS_CELSIUS,       // units
                                              astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                              ASTL_CATEGORY_UNCATEGORIZED,            // category
                                              astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                              std::move(groups),                      // metric groups
                                              CollectorType::SCMI,                    // collector type
                                              astl::NullOperationBuilder{}            // data_event_ids
  );
  auto cfg_b = std::make_unique<MetricConfig>("metricB",                              // name
                                              "descr",                                // description
                                              astl_units_t::ASTL_UNITS_CELSIUS,       // units
                                              astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                              ASTL_CATEGORY_UNCATEGORIZED,            // category
                                              astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                              CollectorType::SCMI,                    // collector type
                                              // no groups
                                              astl::NullOperationBuilder{}  // data_event_ids
  );

  MockTarget  target;
  std::string target_name{"AP0"};
  ALLOW_CALL(target, Name()).RETURN(target_name);
  astl_status_code status = mgr.RegisterMetric(std::move(cfg_a), {&target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  status = mgr.RegisterMetric(std::move(cfg_b), {&target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.GetAvailableMetrics(&target).value().size() == 2);
  REQUIRE(mgr.GetMetricGroups().size() == 1);
  REQUIRE(mgr.GetMetricGroups(&target).value().size() == 1);
  auto bad_arg_result = mgr.GetMetricGroups(nullptr);
  REQUIRE(bad_arg_result.error() == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE(
    "MetricManager::RegisterMetric with 4 metrics, spread into 3 groups across 2 targets"
    "[MetricManager][MetricGroup]") {
  // "thermals" group with 'tempA' on target AP0
  // "throttle" group with 'tempA' and 'throttleA' on target AP0
  // and metric 'voltageA' with no group  on target AP0
  // "NIC" group with 'voltageB' on target BMC

  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  std::vector<std::string> temp_a_groups{"thermals", "throttle"};
  std::vector<std::string> throttle_a_groups{"throttle"};
  std::vector<std::string> voltage_b_groups{"NIC"};

  // 2) Build a MetricConfig whose collector type is SCMI
  auto temp_a = std::make_unique<MetricConfig>("tempA",                                // name
                                               "descr",                                // description
                                               astl_units_t::ASTL_UNITS_CELSIUS,       // units
                                               astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                               ASTL_CATEGORY_UNCATEGORIZED,            // category
                                               astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                               std::move(temp_a_groups),               // metric groups
                                               CollectorType::SCMI,                    // collector type
                                               astl::NullOperationBuilder{}            // data_event_ids
  );

  auto throttle_cfg = std::make_unique<MetricConfig>("throttleA",                            // name
                                                     "descr",                                // description
                                                     astl_units_t::ASTL_UNITS_CELSIUS,       // units
                                                     astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                                     ASTL_CATEGORY_UNCATEGORIZED,            // category
                                                     astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                                     std::move(throttle_a_groups),           // metric groups
                                                     CollectorType::SCMI,                    // collector type
                                                     astl::NullOperationBuilder{}            // data_event_ids
  );

  auto core_voltage_cfg = std::make_unique<MetricConfig>("voltageA",                             // name
                                                         "descr",                                // description
                                                         astl_units_t::ASTL_UNITS_VOLTS,         // units
                                                         astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                                         ASTL_CATEGORY_UNCATEGORIZED,            // category
                                                         astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                                         CollectorType::SCMI,                    // collector type
                                                         // no groups
                                                         astl::NullOperationBuilder{}  // data_event_ids
  );

  // and another metric 'voltageB' with its own T2 group
  auto nic_voltage_cfg = std::make_unique<MetricConfig>("voltageB",                             // name
                                                        "descr",                                // description
                                                        astl_units_t::ASTL_UNITS_VOLTS,         // units
                                                        astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                                        ASTL_CATEGORY_UNCATEGORIZED,            // category
                                                        astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                                        std::move(voltage_b_groups),            // metric groups
                                                        CollectorType::SCMI,                    // collector type
                                                        astl::NullOperationBuilder{}            // data_event_ids
  );

  MockTarget  ap0_target;
  std::string ap0_target_name{"AP0"};
  ALLOW_CALL(ap0_target, Name()).RETURN(ap0_target_name);
  astl_status_code status{ASTL_STATUS_SUCCESS};
  status = mgr.RegisterMetric(std::move(temp_a), {&ap0_target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  status = mgr.RegisterMetric(std::move(throttle_cfg), {&ap0_target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  status = mgr.RegisterMetric(std::move(core_voltage_cfg), {&ap0_target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  MockTarget  bmc_target;
  std::string bmc_target_name{"BMC"};
  ALLOW_CALL(bmc_target, Name()).RETURN(bmc_target_name);
  status = mgr.RegisterMetric(std::move(nic_voltage_cfg), {&bmc_target});
  REQUIRE(status == ASTL_STATUS_SUCCESS);

  // registration done, now verify lookup

  REQUIRE(mgr.GetAvailableMetrics(&ap0_target).value().size() == 3);

  // check target AP0 for 2 groups
  auto ap0_groups = mgr.GetMetricGroups(&ap0_target).value();
  REQUIRE(ap0_groups.size() == 2);  // thermals and throttle groups

  // turn the C-style handles into astl::MetricGroup objects
  std::vector<const astl::MetricGroup*> expected_groups;
  expected_groups.reserve(2);
  std::ranges::transform(ap0_groups, std::back_inserter(expected_groups), [](const astl_metric_group_handle_t group) {
    return astl::MetricGroup::FromApiHandle(group);
  });

  // thermals should have only one: tempA
  auto thermal_group = std::find_if(expected_groups.begin(), expected_groups.end(),
                                    [](const astl::MetricGroup* group) { return group->name == "thermals"; });
  REQUIRE(thermal_group != expected_groups.end());
  REQUIRE((*thermal_group)->metrics.size() == 1);  // tempA only

  // throttle should have tempA and throttleA
  auto throttle_group = std::find_if(expected_groups.begin(), expected_groups.end(),
                                     [](const astl::MetricGroup* group) { return group->name == "throttle"; });
  REQUIRE(throttle_group != expected_groups.end());
  REQUIRE((*throttle_group)->metrics.size() == 2);  // tempA and throttleA

  // now look at the second target, BMC, which should have one group with one metric
  auto bmc_groups = mgr.GetMetricGroups(&bmc_target).value();
  REQUIRE(bmc_groups.size() == 1);  // NIC group only
  std::vector<const astl::MetricGroup*> bmc_expected_groups;
  bmc_expected_groups.reserve(1);
  // turn the C-style handles into astlMetricGroup objects
  std::ranges::transform(
      bmc_groups, std::back_inserter(bmc_expected_groups),
      [](const astl_metric_group_handle_t group) { return astl::MetricGroup::FromApiHandle(group); });
  REQUIRE(bmc_expected_groups[0]->metrics.size() == 1);  // NIC only
}
