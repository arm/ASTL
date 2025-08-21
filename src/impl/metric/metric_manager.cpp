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
#include "delta_metric.hpp"
#include "i_metric.hpp"
#include "metric_config.hpp"
#include "rate_metric.hpp"
#include "sampled_value_metric.hpp"

namespace astl {

/**
 * @brief Helper to instantiate a metric based on its type
 *
 */
auto CreateMetricFromConfig(const MetricConfig* metric_config)
    -> std::expected<std::unique_ptr<IMetric>, astl_status_code> {
  switch (metric_config->MetricType()) {
    case astl_metric_type_t::ASTL_METRIC_VALUE:
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating SampledValue metric '{}'", metric_config->Name());
      return std::make_unique<SampledValueMetric>(metric_config->Name().c_str(), metric_config->Description().c_str(),
                                                  metric_config->Units(), metric_config->ValueType());
      break;
    case astl_metric_type_t::ASTL_METRIC_DELTA:
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating DeltaMetric '{}'", metric_config->Name());
      return std::make_unique<DeltaMetric>(metric_config->Name().c_str(), metric_config->Description().c_str(),
                                           metric_config->Units(), metric_config->ValueType());
      break;
    case astl_metric_type_t::ASTL_METRIC_RATE:
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating RateMetric '{}'", metric_config->Name());
      return std::make_unique<RateMetric>(metric_config->Name().c_str(), metric_config->Description().c_str(),
                                          metric_config->Units(), metric_config->ValueType());
      break;
    // TODO (https://jira.arm.com/browse/ASTL-102):
    // handle additional MetricType cases here
    default:
      // Unknown metric type; ignore or log an error.
      ASTL_LOG_ERROR("CreateMetricFromConfig: unknown metric type received: {}", metric_config->MetricType());
      return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

/**
 * @brief Helper to look up a IMetric handle for a specific target from a metric API handle
 */
auto GetMetricOnTarget(astl_metric_handle_t metric_handle,
                       const ITarget*       target) -> std::expected<IMetric*, astl_status_code> {
  auto* metric_details = static_cast<MetricHandle*>(metric_handle);
  if (!metric_details) {
    ASTL_LOG_ERROR("GetMetricOnTarget: Invalid metric handle {}", metric_handle);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  auto target_iter = metric_details->target_to_metric_map.find(target);
  if (target_iter == metric_details->target_to_metric_map.end()) {
    ASTL_LOG_ERROR("GetMetricOnTarget: Target '{}' not found for metric handle {}", target->Name(), metric_handle);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  if (!target_iter->second) {
    ASTL_LOG_ERROR("GetMetricOnTarget: No metric found for target '{}' and metric handle {}", target->Name(),
                   metric_details->config->Name());
    return std::unexpected{ASTL_STATUS_INTERNAL_ERROR};
  }
  return target_iter->second.get();
}

astl_status_code MetricManager::RegisterMetric(std::unique_ptr<MetricConfig>      metric_config,
                                               std::vector<const ITarget*> const& targets) {
  CollectorType collector_type = metric_config->GetCollectorType();
  if (!IsCollectorTypeSupported(collector_type)) {
    return astl_status_code::ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE;
  }

  std::unordered_map<const ITarget*, std::unique_ptr<IMetric>> target_specific_metrics;
  for (const auto& target : targets) {
    // Register the metric based on its type and add it to the _metric_handles vector and
    // metric config mappings.
    auto metric_or_error = CreateMetricFromConfig(metric_config.get());
    if (!metric_or_error.has_value()) {
      return metric_or_error.error();
    }
    auto metric                     = std::move(metric_or_error.value());
    target_specific_metrics[target] = std::move(metric);
  }

  _metric_handles.emplace_back(
      std::make_unique<MetricHandle>(std::move(metric_config), /* targets, */ std::move(target_specific_metrics)));

  astl_metric_handle_t metric_handle = static_cast<astl_metric_handle_t>(_metric_handles.back().get());

  _metric_api_handles.push_back(metric_handle);

  for (const auto* const target : targets) {
    _target_to_metrics_map[target].push_back(metric_handle);
  }
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::GetAvailableMetrics() const
    -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> {
  // Create a span over the internal metric handle vector
  std::span<const astl_metric_handle_t> handles_span(_metric_api_handles);
  return std::expected<std::span<const astl_metric_handle_t>, astl_status_code>(std::in_place, handles_span);
}

auto MetricManager::GetAvailableMetrics(const ITarget* target) const
    -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> {
  const auto target_iter = _target_to_metrics_map.find(target);
  if (target_iter == _target_to_metrics_map.end()) {
    ASTL_LOG_ERROR("GetAvailableMetrics: Target '{}' not found.", target->Name());
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  std::span<const astl_metric_handle_t> handles_span(target_iter->second);
  return std::expected<std::span<const astl_metric_handle_t>, astl_status_code>(std::in_place, handles_span);
}

/**
 * @brief Assign values such as name, units, etc to the given properties pointer.
 */
auto MetricManager::GetProperties(astl_metric_handle_t      metric,
                                  astl_metric_properties_t* properties) const -> astl_status_code {
  auto* metric_details = static_cast<MetricHandle*>(metric);
  if (!metric_details) {
    ASTL_LOG_ERROR("GetProperties: Invalid metric handle {}", metric);
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto first_metric_instance = metric_details->target_to_metric_map.begin();
  if (first_metric_instance == metric_details->target_to_metric_map.end()) {
    ASTL_LOG_ERROR("GetProperties: No metric config found for handle {}", metric);
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  first_metric_instance->second->GetProperties(properties);
  properties->_handle = metric;  // the properties needs to be associated with the MetricHandle
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::GetRequiredOperations(std::span<const astl_metric_handle_t> metrics, const ITarget* target)
    -> std::expected<CollectionOperations, astl_status_code> {
  /**
   * This method performs the following steps for each given metric:
   * - Validates each metric is registered (returns BAD_ARGUMENT if not).
   * - Ensures each metric uses an known collector (returns UNSUPPORTED_COLLECTOR_TYPE otherwise).
   * - For each DataEventId in the metric's configuration:
   * - Parses the string ID to a uint32_t, handling exceptions.
   * - Creates a ScmiReadOperation for that event ID.
   * - Records the operation_id to metric mapping for processing samples later.
   * - Returns the complete CollectionOperations struct or an appropriate error.
   * */

  OperationSequence op_sequence;

  for (auto* metric_api_handle : metrics) {
    if (std::ranges::find(_metric_api_handles, metric_api_handle) == _metric_api_handles.end()) {
      ASTL_LOG_ERROR("GetRequiredOperations: unrecognized astl_metric_handle {}", metric_api_handle);
      return std::unexpected(ASTL_STATUS_INVALID_METRIC_HANDLE);
    }
    auto*       metric_handle = static_cast<MetricHandle*>(metric_api_handle);
    const auto& config        = metric_handle->config;

    if (!IsCollectorTypeSupported(config->GetCollectorType())) {
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }

    // Currently, only SCMI collector type is supported for metrics.
    // MetricConfig provides a DataEventIds field specifying the SCMI event IDs.
    // TODO (https://jira.arm.com/browse/ASTL-114): handle other collector types
    const auto& data_event_ids      = config->DataEventIds();
    auto        target_and_event_id = data_event_ids.find(target->Name());

    if (target_and_event_id == data_event_ids.end()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Target '{}' not found in DataEventIds for metric '{}'", target->Name(),
                     config->Name());
      return std::unexpected{ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET};
    }

    auto metric_or_error = GetMetricOnTarget(metric_handle, target);
    if (!metric_or_error.has_value()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Failed to get metric {} on target {}", config->Name(), target->Name());
      return std::unexpected{metric_or_error.error()};
    }
    IMetric* metric = *metric_or_error;

    auto                               event_id     = target_and_event_id->second;
    std::unique_ptr<ScmiReadOperation> operation    = std::make_unique<ScmiReadOperation>(event_id);
    uint32_t                           operation_id = operation->GetId();  // or operation->operation_id if public
    _operation_to_metric_map[operation_id]          = metric;
    op_sequence.push_back(std::move(operation));
    ASTL_LOG_INFO("GetRequiredOperations: Created Operation for event ID {:04X}", event_id);
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
    // Process the sample and propagate errors
    // TODO (https://jira.arm.com/browse/ASTL-130): MetricManager needs to ensure Monotonicity in timestamp.
    astl_status_code status = metric_handle->ReceiveSample(sample);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ProcessData: Failed to process sample for operation ID {} with status {}", sample.operation_id,
                     astlStatusString(status));
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Retrieve the collected samples for the given target and metric,
 *        or an error if the target+metric combination isn't valid
 */
auto MetricManager::GetSamples(astl_metric_handle_t metric_handle, const ITarget* target)
    -> std::expected<std::span<const astl::SampledData>, astl_status_code> {
  auto metric_or_error = GetMetricOnTarget(metric_handle, target);
  if (!metric_or_error.has_value()) {
    ASTL_LOG_ERROR("GetSamples: Failed to get metric on target {} for handle {}", target->Name(), metric_handle);
    return std::unexpected{metric_or_error.error()};
  }
  const IMetric* metric         = *metric_or_error;
  auto           samples_result = metric->GetSamples();
  return samples_result;
}

astl_status_code MetricManager::SummarizeMetrics() {
  _operation_to_metric_map.clear();  // release the memory tying operation IDs to metrics
  for (const auto& metric_details : _metric_handles) {
    for (auto& [target, metric] : metric_details->target_to_metric_map) {
      astl_status_code status = metric->Summarize();
      if (status != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("SummarizeMetrics: Summarize failed on target {} for metric {} with status {}", target->Name(),
                       metric->Name(), astlStatusString(status));
        return status;
      }
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
