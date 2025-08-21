/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "astl/astl_telemetry.h"
#include "common/capabilities.hpp"
#include "common/scmi/scmi_read_operation.hpp"
#include "metric/i_metric.hpp"
#include "metric/metric_config.hpp"
#include "metric/metric_manager.hpp"
#include "metric/sampled_value_metric.hpp"

using astl::Capabilities;
using astl::CollectorCapability;
using astl::CollectorType;
using astl::IMetric;
using astl::MetricConfig;
using astl::MetricManager;
using astl::OperationSequence;
using astl::SampledData;
using astl::ScmiTargetToDataEventIdMap;
using astl::SystemCapability;

constexpr size_t kJunk = 7;
namespace astl {
// Test accessor for MetricManager internals
class MetricManagerTestAccessor {
 public:
  static void InjectMetric(astl::MetricManager& mgr, std::unique_ptr<astl::IMetric> metric,
                           std::unique_ptr<astl::MetricConfig> cfg, const astl::ITarget* target) {
    std::unordered_map<const ITarget*, std::unique_ptr<IMetric>> target_to_metric;
    auto metric_handle = std::make_unique<astl::MetricHandle>(std::move(cfg), std::move(target_to_metric));
    metric_handle->target_to_metric_map[target] = std::move(metric);
    astl_metric_handle_t metric_api_handle      = static_cast<astl_metric_handle_t>(metric_handle.get());
    mgr._metric_handles.push_back(std::move(metric_handle));
    mgr._metric_api_handles.push_back(metric_api_handle);
    mgr._target_to_metrics_map[target].push_back(metric_api_handle);
  }

  static void InjectOperation(astl::MetricManager& mgr, OperationId op_id, IMetric* metric_handle) {
    // In a real implementation, this would add the operation to the manager's internal state.
    mgr._operation_to_metric_map[op_id] = metric_handle;
  }
};
}  // namespace astl

// Dummy metric implementation for testing purposes
// This metric simply collects samples and stores them in a vector.
// It can be configured to return a specific status code when processing samples.
struct TestMetric : public IMetric {
  astl_status_code         lastStatus      = ASTL_STATUS_SUCCESS;
  astl_status_code         summarizeStatus = ASTL_STATUS_SUCCESS;
  std::vector<SampledData> received;

  astl_status_code ReceiveSample(const SampledData& sample) override {
    received.push_back(sample);
    return lastStatus;
  }
  // --- Implement remaining pure-virtuals so TestMetric is concrete ---
  bool CheckCapabilities(const Capabilities& /*caps*/) const override { return true; }

  std::expected<OperationSequence, astl_status_code> GetOperations() override {
    // Return an empty sequence by default
    return OperationSequence{};
  }

  std::span<const SampledData> GetSamples() const override { return {received}; }

  void Reset() override { received.clear(); }

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
  // 2) Retrieve available metrics via GetAvailableMetrics()
  // 3) Fetch required operations and verify we get exactly one ScmiReadOperation with ID==0x123
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // 2) Build a MetricConfig whose collector type is SCMI
  auto cfg = std::make_unique<MetricConfig>("metricA",                              // name
                                            "descr",                                // description
                                            astl_units_t::ASTL_UNITS_CELSIUS,       // units
                                            astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                            astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                            CollectorType::SCMI,                    // collector type
                                            ScmiTargetToDataEventIdMap{}            // data_event_ids
  );

  astl_status_code status = mgr.RegisterMetric(std::move(cfg), {});
  REQUIRE(status == ASTL_STATUS_SUCCESS);
}

TEST_CASE("MetricManager::RegisterMetric fails when collector unsupported", "[MetricManager]") {
  // 1) Manager only supports SCMI
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // 2) Build a MetricConfig whose collector type is MMIO - Not supported
  auto cfg = std::make_unique<MetricConfig>("metricB", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                            astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                            CollectorType::MMIO, ScmiTargetToDataEventIdMap{});

  astl_status_code status = mgr.RegisterMetric(std::move(cfg), {});
  REQUIRE(status == ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE);
}

TEST_CASE("MetricManager::GetRequiredOperations succeeds with valid SCMI metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  auto          target = std::make_unique<MockTarget>();
  std::string   target_name{"AP0"};
  ALLOW_CALL(*target, Name()).RETURN(target_name);

  auto cfg = std::make_unique<MetricConfig>("metricA", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                            astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                            CollectorType::SCMI,
                                            ScmiTargetToDataEventIdMap{
                                                {target_name, 0x123}
  });

  REQUIRE(mgr.RegisterMetric(std::move(cfg), {target.get()}) == ASTL_STATUS_SUCCESS);
  // Retrieve the registered metrics
  auto avail_or_error = mgr.GetAvailableMetrics();
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
  auto* const                               unregistered_metric = static_cast<astl_metric_handle_t>(&junkval);
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
  std::unique_ptr<astl::IMetric> owner_metric_mmio(new astl::SampledValueMetric(
      "metricA", "descr", astl_units_t::ASTL_UNITS_CELSIUS, astl_value_type_t::ASTL_VALUE_UINT64));

  // Manually associate the metric pointer
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric_mmio),
      std::make_unique<MetricConfig>("metricC", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                     astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::MMIO,
                                     ScmiTargetToDataEventIdMap{
                                         {"AP0", 0x123}
  }),
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
  auto cfg0 = std::make_unique<MetricConfig>("metric0", "description0", astl_units_t::ASTL_UNITS_CELSIUS,
                                             astl_value_type_t::ASTL_VALUE_UINT64,
                                             astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                             ScmiTargetToDataEventIdMap{
                                                 {target0_name, 0x123}
  });
  auto cfg1 = std::make_unique<MetricConfig>("metric1", "description1", astl_units_t::ASTL_UNITS_WATTS,
                                             astl_value_type_t::ASTL_VALUE_UINT64,
                                             astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                             ScmiTargetToDataEventIdMap{
                                                 {target1_name, 0x456}
  });
  // metric01 can be collected on either target 0 or 1
  auto cfg01 = std::make_unique<MetricConfig>("metric01", "description1", astl_units_t::ASTL_UNITS_AMPS,
                                              astl_value_type_t::ASTL_VALUE_FLOAT64,
                                              astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                              ScmiTargetToDataEventIdMap{
                                                  {target0_name, 0xACED},
                                                  {target1_name, 0xBABE}
  });

  // Create a Metric Manager
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);
  REQUIRE(mgr.RegisterMetric(std::move(cfg0), {target0.get()}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterMetric(std::move(cfg1), {target1.get()}) == ASTL_STATUS_SUCCESS);
  REQUIRE(mgr.RegisterMetric(std::move(cfg01), {target0.get(), target1.get()}) == ASTL_STATUS_SUCCESS);

  // Retrieve the registered metrics
  auto avail_or_error = mgr.GetAvailableMetrics();
  REQUIRE(avail_or_error.has_value());
  auto metrics = *avail_or_error;
  // cfg0 + cfg1 + cfg01 (is one metric that can run on 2 targets: that still only counts as one.)
  REQUIRE(metrics.size() == 3);

  SECTION("MetricManager::GetRequiredOperations for 1 metric on multiple targets") {
    auto        multi_target_metric = metrics.subspan(2, 1);  // get the metric for cfg01
    auto const& ops_target0         = mgr.GetRequiredOperations(multi_target_metric, target0.get());
    REQUIRE(ops_target0);
    astl::OperationSequence const& op_seq0 = ops_target0->operationsOnSample;
    REQUIRE(op_seq0.size() == 1);  // the DE ID for target0
    const auto& base_op0 = op_seq0.front();
    const auto* scmi_op0 = dynamic_cast<astl::ScmiReadOperation*>(base_op0.get());
    REQUIRE(scmi_op0->scmi_data_event_id == 0xACED);

    auto const& ops_target1 = mgr.GetRequiredOperations(multi_target_metric, target1.get());
    REQUIRE(ops_target1);
    astl::OperationSequence const& op_seq1 = ops_target1->operationsOnSample;
    REQUIRE(op_seq1.size() == 1);  // the DE ID for target1
    const auto& base_op1 = op_seq1.front();
    const auto* scmi_op1 = dynamic_cast<astl::ScmiReadOperation*>(base_op1.get());
    REQUIRE(scmi_op1->scmi_data_event_id == 0xBABE);

    REQUIRE(base_op0->GetId() != base_op1->GetId());
  }

  SECTION("MetricManager::GetRequiredOperations for metric on unsupported target") {
    auto        metric0     = metrics.subspan(0, 1);  // get the metric for cfg0
    auto const& ops_target1 = mgr.GetRequiredOperations(metric0, target1.get());
    REQUIRE(!ops_target1);
    REQUIRE(ops_target1.error() == ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET);
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
                                     astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, ScmiTargetToDataEventIdMap{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, op_id, metric_ptr);

  astl::AstlValue          val1{uint64_t{256}};  // Sample value
  astl::SampledData        sample1(op_id, val1);
  std::vector<SampledData> data{sample1};
  REQUIRE(mgr.ProcessData(data) == ASTL_STATUS_SUCCESS);
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
                                     astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, ScmiTargetToDataEventIdMap{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, op_id, metric_ptr);

  astl::AstlValue          val1{uint64_t{100}};
  astl::AstlValue          val2{uint64_t{200}};
  SampledData              sample1(op_id, val1);
  SampledData              sample2(op_id, val2);
  std::vector<SampledData> data{sample1, sample2};

  REQUIRE(mgr.ProcessData(data) == ASTL_STATUS_SUCCESS);
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
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     ScmiTargetToDataEventIdMap{}),
      &target);
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric2),
      std::make_unique<MetricConfig>("m2", "desc", astl_units_t::ASTL_UNITS_NONE, astl_value_type_t::ASTL_VALUE_UINT64,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     ScmiTargetToDataEventIdMap{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 1, metric_ptr1);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 2, metric_ptr2);

  astl::AstlValue          val1{uint64_t{5}};
  astl::AstlValue          val2{uint64_t{7}};
  SampledData              sample1(1, val1);
  SampledData              sample2(2, val2);
  SampledData              sample3(1, val1);
  SampledData              sample4(2, val2);
  std::vector<SampledData> data{sample1, sample2, sample3, sample4};

  REQUIRE(mgr.ProcessData(data) == ASTL_STATUS_SUCCESS);
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
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     ScmiTargetToDataEventIdMap{}),
      &target);
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric2),
      std::make_unique<MetricConfig>("m2", "desc", astl_units_t::ASTL_UNITS_NONE, astl_value_type_t::ASTL_VALUE_UINT64,
                                     astl_metric_type_t::ASTL_METRIC_VALUE, CollectorType::SCMI,
                                     ScmiTargetToDataEventIdMap{}),
      &target);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 1, metric_ptr1);
  astl::MetricManagerTestAccessor::InjectOperation(mgr, 2, metric_ptr2);

  astl::AstlValue          val1{uint64_t{1}};
  astl::AstlValue          val2{uint64_t{2}};
  SampledData              sample1(1, val1);
  SampledData              sample2(2, val2);
  SampledData              sample3(1, val1);
  std::vector<SampledData> data{sample1, sample2, sample3};

  REQUIRE(mgr.ProcessData(data) == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
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
                                     astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, ScmiTargetToDataEventIdMap{}),
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
                                     astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::SCMI, ScmiTargetToDataEventIdMap{}),
      &target);

  REQUIRE(mgr.SummarizeMetrics() == ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}