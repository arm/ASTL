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

#include "metric_manager.hpp"

#include <cstdint>
#include <string>

#include "astl/astl_errors.h"
#include "collector/collection_operations.hpp"
#include "common/scmi/scmi_read_operation.hpp"
#include "i_metric.hpp"
#include "metric_config.hpp"
#include "sampled_value_metric.hpp"

namespace astl {

astl_status_code MetricManager::RegisterMetric(std::unique_ptr<MetricConfig> metric_config) {
  CollectorType collector_type = metric_config->GetCollectorType();
  if (!IsCollectorTypeSupported(collector_type)) {
    return astl_status_code::ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE;
  }
  // Register the metric based on its type and add it to the _metric_handles vector and
  // metric config mappings.
  switch (metric_config->MetricType()) {
    case astl_metric_type_t::ASTL_METRIC_VALUE: {
      auto metric =
          std::make_unique<SampledValueMetric>(metric_config->Name().c_str(), metric_config->Description().c_str(),
                                               metric_config->Units(), metric_config->ValueType());
      ASTL_LOG_INFO("RegisterMetric: Registered metric '{}'", metric_config->Name());
      IMetric* metric_ptr = metric.get();
      _config_map.emplace(metric_ptr, std::move(metric_config));
      _metrics_map.emplace(metric_ptr, std::move(metric));
      _metric_handles.emplace_back(metric_ptr);
      break;
    }
    // TODO (https://jira.arm.com/browse/ASTL-102):
    // handle additional MetricType cases here
    default: {
      // Unknown metric type; ignore or log an error.
      ASTL_LOG_ERROR("RegisterMetric: unknown metric type received: {}", static_cast<int>(metric_config->MetricType()));
      return ASTL_STATUS_NOT_IMPLEMENTED;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

std::expected<std::span<IMetric* const>, astl_status_code> MetricManager::GetAvailableMetrics() const {
  // Create a span over the internal metric handle vector
  std::span<IMetric* const> handles_span(_metric_handles);
  return std::expected<std::span<IMetric* const>, astl_status_code>(std::in_place, handles_span);
}

std::expected<CollectionOperations, astl_status_code> MetricManager::GetRequiredOperations(
    std::span<IMetric* const> metrics) {
  /**
   * This method performs the following steps:
   * - Validates each metric pointer is registered (returns BAD_ARGUMENT if not).
   * - Ensures each metric uses an known collector (returns UNSUPPORTED_COLLECTOR_TYPE otherwise).
   * - For each DataEventId in the metric's configuration:
   * - Parses the string ID to a uint32_t, handling exceptions.
   * - Creates a ScmiReadOperation for that event ID.
   * - Records the operation_id to metric mapping for processing samples later.
   * - Returns the complete CollectionOperations struct or an appropriate error.
   * */

  OperationSequence op_sequence;

  for (auto* metric : metrics) {
    auto iterator = _config_map.find(metric);
    if (iterator == _config_map.end()) {
      ASTL_LOG_ERROR("GetRequiredOperations: metric not found in config map");
      return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
    }

    const MetricConfig& config = *iterator->second;

    if (!IsCollectorTypeSupported(config.GetCollectorType())) {
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }

    // Currently, only SCMI collector type is supported for metrics.
    // MetricConfig provides a DataEventIds field specifying the SCMI event IDs.
    // TODO (https://jira.arm.com/browse/ASTL-114): handle other collector types
    const std::vector<std::string>& data_event_ids = config.DataEventIds();
    for (const auto& id_string : data_event_ids) {
      uint32_t event_id = 0;
      try {
        event_id = static_cast<uint32_t>(std::stoul(id_string));
      } catch (const std::exception& e) {
        ASTL_LOG_ERROR("GetRequiredOperations: invalid event ID string '{}' (exception: {})", id_string, e.what());
        return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
      }
      std::unique_ptr<ScmiReadOperation> operation    = std::make_unique<ScmiReadOperation>(event_id);
      uint32_t                           operation_id = operation->GetId();  // or operation->operation_id if public
      _operation_to_metric_map[operation_id]          = metric;
      op_sequence.push_back(std::move(operation));
      ASTL_LOG_INFO("GetRequiredOperations: Created Operation for event ID {}", event_id);
    }
  }

  CollectionOperations operations{.operationsBeforeStart{},
                                  .operationsAtStart{},
                                  .operationsOnSample{std::move(op_sequence)},
                                  .operationsAtStop{},
                                  .samplingInterval{},
                                  .requirements{astl::CollectorCapability{astl::CollectorType::SCMI}}};
  return operations;
}

astl_status_code MetricManager::ProcessData(std::span<SampledData> data) {
  for (const auto& sample : data) {
    // Find the metric handle corresponding to this operation ID
    auto op_iter = _operation_to_metric_map.find(sample.operation_id);
    if (op_iter == _operation_to_metric_map.end()) {
      ASTL_LOG_ERROR("ProcessData: No metric associated with operation ID {}", sample.operation_id);
      return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
    }
    IMetric* metric_handle = op_iter->second;
    // Look up the owning unique_ptr for this metric to invoke ReceiveSample
    auto metric_iter = _metrics_map.find(metric_handle);
    if (metric_iter == _metrics_map.end()) {
      ASTL_LOG_ERROR("ProcessData: Metric pointer not found in metrics map for operation ID {}", sample.operation_id);
      return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
    }
    // Process the sample and propagate errors
    astl_status_code status = metric_iter->second->ReceiveSample(sample);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ProcessData: Failed to process sample for operation ID {} with status {}", sample.operation_id,
                     astlStatusString(status));
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

astl_status_code MetricManager::SummarizeMetrics() {
  _operation_to_metric_map.clear();  // release the memory tying operation IDs to metrics
  for (IMetric* metric_ptr : _metric_handles) {
    auto it = _metrics_map.find(metric_ptr);
    if (it == _metrics_map.end()) {
      ASTL_LOG_ERROR("SummarizeMetrics: Metric pointer not found in metrics map");
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    astl_status_code status = it->second->Summarize();
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("SummarizeMetrics: Summarize failed for metric with status {}", astlStatusString(status));
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

bool MetricManager::IsCollectorTypeSupported(CollectorType required_collector_type) const {
  // Check against the manager's capabilities
  const std::vector<CollectorCapability>& collector_caps = _capabilities.GetCollectorCapability();
  return std::any_of(collector_caps.begin(), collector_caps.end(),
                     [&](const CollectorCapability& cap) { return cap.GetCollectorType() == required_collector_type; });
}
}  // namespace astl
