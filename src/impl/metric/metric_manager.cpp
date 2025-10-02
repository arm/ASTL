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
#include "common/astl_defines.hpp"
#include "delta_metric.hpp"
#include "finite_set_metric.hpp"
#include "i_metric.hpp"
#include "metric_config.hpp"
#include "operation/scmi_read_operation.hpp"
#include "rate_metric.hpp"
#include "residency_metric.hpp"
#include "sampled_value_metric.hpp"

namespace astl {

astl_status_code MetricManager::SinkProcessedSamples(const IMetric*                        metric,
                                                     std::span<const ProcessedSampledData> processed_samples) {
  auto target_or_error = GetTargetForMetric(metric);
  if (!target_or_error.has_value()) {
    return target_or_error.error();
  }
  return SinkProcessedSamples(*target_or_error, metric, processed_samples);
}

/**
 * @brief Helper to instantiate a metric based on its type
 *
 * @todo (https://jira.arm.com/browse/ASTL-170) Remove downcasting of ResidencyMetricConfig and target parameter used
 * only for residency metrics. Consider splitting into CreateMetricFromConfig (for simple metrics) and
 * CreateResidencyMetricFromConfig (for residency metrics with target-specific configuration), or implement visitor
 * pattern to maintain abstraction without dynamic_cast.
 */
auto CreateMetricFromConfig(const MetricConfig* metric_config, const ITarget* target, IProcessedSampleSink* sink)
    -> std::expected<std::unique_ptr<IMetric>, astl_status_code> {
  if (!target) {
    ASTL_LOG_ERROR("CreateMetricFromConfig: Invalid target");
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  if (!metric_config) {
    ASTL_LOG_ERROR("CreateMetricFromConfig: Invalid metric config");
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  const auto  metric_type = metric_config->MetricType();
  const auto& metric_name = metric_config->Name();

  switch (metric_type) {
    case astl_metric_type_t::ASTL_METRIC_VALUE:
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating SampledValue metric '{}'", metric_name);
      return std::make_unique<SampledValueMetric>(metric_config, target, sink);
      break;

    case astl_metric_type_t::ASTL_METRIC_DELTA:
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating DeltaMetric '{}'", metric_name);
      return std::make_unique<DeltaMetric>(metric_config, target, sink);
      break;

    case astl_metric_type_t::ASTL_METRIC_RATE:
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating RateMetric '{}'", metric_name);
      return std::make_unique<RateMetric>(metric_config, target, sink);
      break;

    case astl_metric_type_t::ASTL_METRIC_RESIDENCY: {
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating ResidencyMetric '{}'", metric_name);

      // Cast to ResidencyMetricConfig to get state configurations
      const auto* residency_config = dynamic_cast<const ResidencyMetricConfig*>(metric_config);
      if (!residency_config) {
        ASTL_LOG_ERROR("CreateMetricFromConfig: Failed to cast to ResidencyMetricConfig for metric '{}'", metric_name);
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }

      // Create state configurations from the metric config for the specific target
      std::vector<ResidencyMetricConfig::StateInfo> state_configs;
      const auto&                                   state_info = residency_config->GetStateInfo();

      // Get states for the specific target
      if (auto target_iter = state_info.find(target->Name()); (target_iter != state_info.end())) {
        const auto& target_states = target_iter->second;
        std::ranges::transform(target_states, std::back_inserter(state_configs), [](const auto& state_pair) {
          const auto& [state_name, state_data] = state_pair;
          return ResidencyMetricConfig::StateInfo{state_name, state_data.tick_frequency, state_data.operation_builder};
        });
      } else {
        ASTL_LOG_ERROR("CreateMetricFromConfig: No state configurations found for target '{}' in metric '{}'",
                       target->Name(), metric_name);
        return std::unexpected(ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET);
      }
      return std::make_unique<ResidencyMetric>(residency_config, state_configs, target, sink);
    }
    case astl_metric_type_t::ASTL_METRIC_FINITE_SET_VALUE: {
      ASTL_LOG_INFO("CreateMetricFromConfig: Creating FiniteSetMetric '{}'", metric_name);

      // Cast to FiniteSetMetricConfig to get finite set configuration
      const auto* finite_set_config = dynamic_cast<const FiniteSetMetricConfig*>(metric_config);
      if (!finite_set_config) {
        ASTL_LOG_ERROR("CreateMetricFromConfig: Failed to cast to FiniteSetMetricConfig for metric '{}'", metric_name);
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      return std::make_unique<FiniteSetMetric>(finite_set_config, target, sink);
    }
    // TODO (https://jira.arm.com/browse/ASTL-102):
    // handle additional MetricType cases here
    default:
      // Unknown metric type; ignore or log an error.
      ASTL_LOG_ERROR("CreateMetricFromConfig: unknown metric type received: {}", metric_type);
      return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

/**
 * @brief Helper to look up a IMetric handle for a specific target from a metric API handle
 */
auto MetricManager::GetMetricOnTarget(astl_metric_handle_t metric_handle, const ITarget* target)
    -> std::expected<IMetric*, astl_status_code> {
  const auto* metric_details = static_cast<const MetricHandle*>(metric_handle);

  if (!metric_details) {
    ASTL_LOG_ERROR("GetMetricOnTarget: Invalid metric handle {}", metric_handle);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  auto target_iter = metric_details->target_to_metric_map.find(target);
  if (target_iter == metric_details->target_to_metric_map.end()) {
    ASTL_LOG_ERROR("GetMetricOnTarget: Target '{}' not found for metric handle {}", target->Name(), metric_handle);
    return std::unexpected{ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET};
  }
  if (!target_iter->second) {
    ASTL_LOG_ERROR("GetMetricOnTarget: No metric found for target '{}' and metric handle {}", target->Name(),
                   metric_details->config->Name());
    return std::unexpected{ASTL_STATUS_INTERNAL_ERROR};
  }
  return target_iter->second.get();
}

astl_status_code MetricManager::RegisterProcessedSampleSink(IProcessedSampleSink* sink) {
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  _registered_processed_sample_sinks.insert(sink);
  return ASTL_STATUS_SUCCESS;
}

astl_status_code MetricManager::UnregisterProcessedSampleSink(IProcessedSampleSink* sink) {
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  // Unregistration is a no-op if the sink was not registered. no need to check for num_removed
  _registered_processed_sample_sinks.erase(sink);

  return ASTL_STATUS_SUCCESS;
}

astl_status_code MetricManager::RegisterMetric(std::unique_ptr<MetricConfig>      metric_config,
                                               std::vector<const ITarget*> const& targets) {
  ASTL_LOG_TRACE("RegisterMetric {} on {} targets", metric_config ? metric_config->Name() : "<null>", targets.size());
  CollectorType collector_type = metric_config->GetCollectorType();
  if (!IsCollectorTypeSupported(collector_type)) {
    return astl_status_code::ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE;
  }

  //  build the target-specific metric instances and associate them with the metric handle.
  std::unordered_map<const ITarget*, std::unique_ptr<IMetric>> target_specific_metrics;
  for (const auto& target : targets) {
    // Register the metric based on its type and add it to the _metric_handles vector and
    // metric config mappings.
    auto metric_or_error = CreateMetricFromConfig(metric_config.get(), target, this);

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
    std::string targets;
    for (const auto& target_metrics : _target_to_metrics_map) {
      targets.append(target_metrics.first->Name() + ", ");
    }
    ASTL_LOG_ERROR("GetAvailableMetrics: Target '{}' not found in '{}'.", target->Name(), targets);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  std::span<const astl_metric_handle_t> handles_span(target_iter->second);
  return std::expected<std::span<const astl_metric_handle_t>, astl_status_code>(std::in_place, handles_span);
}

/**
 * @brief Assign values such as name, units, etc to the given properties pointer.
 */
auto MetricManager::GetProperties(astl_metric_handle_t metric, astl_metric_properties_t* properties) const
    -> astl_status_code {
  const auto* metric_details = static_cast<const MetricHandle*>(metric);
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
   * - For each given metric, asks for the sequence of operations needed to provide sample
   * - Records the operation_id to metric mapping for processing samples later.
   * - Returns the complete CollectionOperations struct or an appropriate error.
   * */

  OperationSequence op_sequence;

  std::optional<CollectorType> collector_type;

  for (const auto* metric_api_handle : metrics) {
    if (std::ranges::find(_metric_api_handles, metric_api_handle) == _metric_api_handles.end()) {
      ASTL_LOG_ERROR("GetRequiredOperations: unrecognized astl_metric_handle {}", metric_api_handle);
      return std::unexpected(ASTL_STATUS_INVALID_METRIC_HANDLE);
    }
    const auto* metric_handle = static_cast<const MetricHandle*>(metric_api_handle);
    const auto& config        = metric_handle->config;

    if (collector_type.has_value() && collector_type != config->GetCollectorType()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Mixed collector types in requested metrics not supported");
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }
    collector_type = config->GetCollectorType();

    if (!IsCollectorTypeSupported(config->GetCollectorType())) {
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }

    auto metric_or_error = GetMetricOnTarget(metric_handle, target);
    if (!metric_or_error.has_value()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Failed to get metric {} on target {}", config->Name(), target->Name());
      return std::unexpected{metric_or_error.error()};
    }
    IMetric* metric = *metric_or_error;

    auto operations_result = metric->GetOperations();
    if (!operations_result.has_value()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Failed to get operations for residency metric '{}'", config->Name());
      return std::unexpected{operations_result.error()};
    }
    auto metric_operations = std::move(operations_result.value());
    for (auto& operation : metric_operations) {
      uint32_t operation_id                  = operation->GetId();
      _operation_to_metric_map[operation_id] = metric;
      op_sequence.push_back(std::move(operation));
      ASTL_LOG_INFO("GetRequiredOperations: Added operation from IMetric::GetOperations() for metric '{}'",
                    config->Name());
    }
  }

  CollectionOperations operations{.operationsBeforeStart{},
                                  .operationsAtStart{},
                                  .operationsOnSample{std::move(op_sequence)},
                                  .operationsAtStop{},
                                  .samplingInterval{},
                                  .requirements{astl::CollectorCapability{collector_type.value()}}};
  return operations;
}

astl_status_code MetricManager::ProcessRawSamples(RawSamplesMap& raw_samples) {
  for (const auto& [target, samples] : raw_samples) {
    for (const auto& sample : samples) {
      // Find the metric handle corresponding to this operation ID
      auto op_iter = _operation_to_metric_map.find(sample.operation_id);
      if (op_iter == _operation_to_metric_map.end()) {
        ASTL_LOG_ERROR("ProcessData: No metric associated with operation ID {}", sample.operation_id);
        return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
      }
      IMetric* metric_handle = op_iter->second;
      // Process the sample and propagate errors
      // TODO (https://jira.arm.com/browse/ASTL-130): MetricManager needs to ensure Monotonicity in timestamp.
      astl_status_code status = metric_handle->ReceiveRawSample(sample);
      if (status != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("ProcessData: Failed to process sample for operation ID {} with status {}", sample.operation_id,
                       astlStatusString(status));
        return status;
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
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

auto MetricManager::GetTargetForMetric(const IMetric* metric) const -> std::expected<const ITarget*, astl_status_code> {
  for (const auto& metric_details : _metric_handles) {
    for (const auto& [target, metric_instance] : metric_details->target_to_metric_map) {
      if (metric_instance.get() == metric) {
        return target;
      }
    }
  }
  ASTL_LOG_ERROR("GetTargetForMetric: Metric instance not found in any registered metrics.");
  return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
}

astl_status_code MetricManager::SinkProcessedSamples(const ITarget* target, const IMetric* metric,
                                                     std::span<const ProcessedSampledData> processed_samples) {
  astl_status_code result = ASTL_STATUS_SUCCESS;
  for (const auto& sink : _registered_processed_sample_sinks) {
    auto sink_result = sink->SinkProcessedSamples(target, metric, processed_samples);
    if (sink_result != ASTL_STATUS_SUCCESS) {
      result = sink_result;  // record last failure and continue
    }
  }
  return result;
}
}  // namespace astl
