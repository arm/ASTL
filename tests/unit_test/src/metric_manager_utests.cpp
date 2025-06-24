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
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <memory>
#include <utility>
#include <vector>

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
using astl::SystemCapability;

namespace astl {
// Test accessor for MetricManager internals
class MetricManagerTestAccessor {
 public:
  static void InjectMetric(astl::MetricManager& mgr, std::unique_ptr<astl::IMetric> metric,
                           std::unique_ptr<astl::MetricConfig> cfg) {
    IMetric* metric_ptr = metric.get();
    mgr._config_map.emplace(metric_ptr, std::move(cfg));
    mgr._metrics_map.emplace(metric_ptr, std::move(metric));
    mgr._metric_handles.emplace_back(metric_ptr);
  }
};
}  // namespace astl

static Capabilities MakeCaps(CollectorType collector_type) {
  // Build a Capabilities object with exactly one collector type.
  // We don’t use the SystemCapability in these tests
  std::vector<CollectorCapability> col_caps{CollectorCapability{collector_type}};
  std::vector<SystemCapability>    sys_caps{SystemCapability{}};
  return Capabilities{std::move(col_caps), std::move(sys_caps)};
}

TEST_CASE("MetricManager::RegisterMetric succeeds when collector supported", "[MetricManager]") {
  // 1) Register a single SCMI metric with data_event_id "123"
  // 2) Retrieve available metrics via GetAvailableMetrics()
  // 3) Fetch required operations and verify we get exactly one ScmiReadOperation with ID==123
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // 2) Build a MetricConfig whose collector type is SCMI
  auto cfg = std::make_unique<MetricConfig>("metricA",                              // name
                                            "descr",                                // description
                                            astl_units_t::ASTL_UNITS_CELSIUS,       // units
                                            astl_value_type_t::ASTL_VALUE_UINT64,   // value type
                                            astl_metric_type_t::ASTL_METRIC_VALUE,  // metric type
                                            CollectorType::SCMI,                    // collector type
                                            std::vector<std::string>{}              // data_event_ids
  );

  astl_status_code status = mgr.RegisterMetric(std::move(cfg));
  REQUIRE(status == ASTL_STATUS_SUCCESS);
}

TEST_CASE("MetricManager::RegisterMetric fails when collector unsupported", "[MetricManager]") {
  // 1) Manager only supports SCMI
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // 2) Build a MetricConfig whose collector type is MMIO - Not supported
  auto cfg = std::make_unique<MetricConfig>("metricB", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                            astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                            CollectorType::MMIO, std::vector<std::string>{});

  astl_status_code status = mgr.RegisterMetric(std::move(cfg));
  REQUIRE(status == ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE);
}

TEST_CASE("MetricManager::GetRequiredOperations succeeds with valid SCMI metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  auto cfg = std::make_unique<MetricConfig>("metricA", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                            astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                            CollectorType::SCMI, std::vector<std::string>{"123"});

  REQUIRE(mgr.RegisterMetric(std::move(cfg)) == ASTL_STATUS_SUCCESS);
  // Retrieve the registered metrics
  std::expected<std::span<IMetric* const>, astl_status_code> avail = mgr.GetAvailableMetrics();
  REQUIRE(avail.has_value());
  auto metrics = *avail;
  REQUIRE(metrics.size() == 1);

  // Obtain the required SCMI operations
  std::expected<astl::OperationSequence, astl_status_code> ops = mgr.GetRequiredOperations(metrics);
  REQUIRE(ops);
  REQUIRE_FALSE(ops->empty());
  // Verify there is exactly one operation with ID 123
  astl::OperationSequence& op_seq = *ops;
  REQUIRE(op_seq.size() == 1);
  auto& base_op = op_seq.front();
  auto* scmi_op = dynamic_cast<astl::ScmiReadOperation*>(base_op.get());
  REQUIRE(scmi_op != nullptr);
  REQUIRE(scmi_op->scmi_data_event_id == 123);
}

TEST_CASE("MetricManager::GetRequiredOperations fails for unregistered metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  // Metric pointer not registered
  astl::IMetric*                                           unregistered_metric = nullptr;
  std::array<astl::IMetric*, 1>                            metrics_array{unregistered_metric};
  std::span<astl::IMetric*>                                span(metrics_array.data(), metrics_array.size());
  std::expected<astl::OperationSequence, astl_status_code> result = mgr.GetRequiredOperations(span);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("MetricManager::GetRequiredOperations fails for non-SCMI metric", "[MetricManager]") {
  Capabilities  caps = MakeCaps(CollectorType::SCMI);
  MetricManager mgr(caps);

  std::unique_ptr<astl::IMetric> owner_metric_mmio(new astl::SampledValueMetric(
      "metricA", "descr", astl_units_t::ASTL_UNITS_CELSIUS, astl_value_type_t::ASTL_VALUE_UINT64));

  // Manually associate the metric pointer
  astl::MetricManagerTestAccessor::InjectMetric(
      mgr, std::move(owner_metric_mmio),
      std::make_unique<MetricConfig>("metricC", "descr", astl_units_t::ASTL_UNITS_CELSIUS,
                                     astl_value_type_t::ASTL_VALUE_UINT64, astl_metric_type_t::ASTL_METRIC_VALUE,
                                     CollectorType::MMIO, std::vector<std::string>{"123"}));

  // Retrieve the metric.
  std::expected<std::span<IMetric* const>, astl_status_code> avail = mgr.GetAvailableMetrics();
  REQUIRE(avail.has_value());
  auto                                                     metric_span = *avail;
  std::expected<astl::OperationSequence, astl_status_code> result      = mgr.GetRequiredOperations(metric_span);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE);
}
