// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
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
/**
 * @brief Helper to look up an ICounter handle representing a counter for a specific target from a metric API handle
 */
auto MetricManager::GetCounterOnTarget(astl_counter_handle_t counter_handle, const ITarget* target) const
    -> std::expected<IMetric*, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto*                 counter_details = static_cast<const CounterHandle*>(counter_handle);
  if (const auto index = std::ranges::find_if(
          _counter_handles, [counter_handle](const auto& handle) { return handle.get() == counter_handle; });
      index == _counter_handles.end()) {
    ASTL_LOG_ERROR("GetCounterOnTarget: Invalid counter handle {}", counter_handle);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  auto target_iter = counter_details->target_to_counter_map.find(target);
  if (target_iter == counter_details->target_to_counter_map.end()) {
    ASTL_LOG_ERROR("GetCounterOnTarget: Target '{:#010x}' not found for counter handle '{:#010x}'",
                   reinterpret_cast<intptr_t>(target), reinterpret_cast<intptr_t>(counter_handle));
    return std::unexpected{ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET};
  }
  return target_iter->second.get();
}

/**
 * @brief Register the counter.
 *
 * This method is called by the orchestrator to register a new counter.
 */
auto MetricManager::RegisterCounter(std::unique_ptr<MetricConfig>      counter_config,
                                    std::vector<const ITarget*> const& targets) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  if (!counter_config) {
    ASTL_LOG_ERROR("RegisterCounter: Invalid counter config");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  ASTL_LOG_TRACE("RegisterCounter {} on {} targets", counter_config->Name(), targets.size());
  CollectorType collector_type = counter_config->GetCollectorType();
  if (!IsCollectorTypeSupported(collector_type)) {
    return ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE;
  }

  // build the target-specific counter instances and associate them with the counter handle.
  std::unordered_map<const ITarget*, std::unique_ptr<ICounter>> target_specific_counters;
  for (const auto& target : targets) {
    // Register the counter based on its type and add it to the _counter_handles vector and
    // counter config mappings.
    std::unique_ptr<ICounter> counter = std::make_unique<Counter>(counter_config.get(), target);
    counter->SetProcessedSampleSink(this);
    target_specific_counters[target] = std::move(counter);
  }
  // store the new CounterHandle
  _counter_handles.emplace_back(
      std::make_unique<CounterHandle>(std::move(counter_config), std::move(target_specific_counters)));

  // make sure all targets that support it are associated with the new counter handle
  astl_counter_handle_t counter_handle = static_cast<astl_counter_handle_t>(_counter_handles.back().get());
  for (const auto* const target : targets) {
    _target_to_counters_map[target].push_back(counter_handle);
  }
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Get the number of available counters for the given target.
 * @param target The target from which to retrieve associated counters
 * @return The number of available counters for the given target, or an error.
 */
auto MetricManager::GetNumAvailableCounters(const ITarget* target) const -> size_t {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto                  target_iter = _target_to_counters_map.find(target);
  if (target_iter == _target_to_counters_map.end()) {
    ASTL_LOG_ERROR("GetNumAvailableCounters: Target '{}' not found", target->Name());
    return 0;
  }
  return target_iter->second.size();
}

/**
 * @brief Get the available counters.
 *
 * This method returns a span of astl_counter_handle_t api handles.
 * This is used to retrieve all the counters that are available for the given target.
 *
 * @param target The target from which to retrieve associated counters
 *
 * @return A span<astl_counter_handle_t> containing all registered counters, or an error.
 */
auto MetricManager::GetAvailableCounters(const ITarget* target) const
    -> std::expected<std::span<const astl_counter_handle_t>, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto                  target_iter = _target_to_counters_map.find(target);
  if (target_iter == _target_to_counters_map.end()) {
    std::string targets;
    for (const auto& target_counters : _target_to_counters_map) {
      targets.append(target_counters.first->Name() + ", ");
    }
    ASTL_LOG_ERROR("GetAvailableCounters: Target '{}' not found in '{}'.", target->Name(), targets);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  std::span<const astl_counter_handle_t> handles_span(target_iter->second);
  return std::expected<std::span<const astl_metric_handle_t>, astl_status_code>(std::in_place, handles_span);
}

/**
 * @brief Assign values such as name, units, etc to the given properties pointer.
 *
 * @param counter The counter API handle for potentially many identical counters that differ only in their target
 * @param properties A non-null pointer to a struct containing that GetProperties will fill in
 *
 * @return An astl_status_code indicating success or ASTL_STATUS_BAD_PARAM
 */
auto MetricManager::GetCounterProperties(astl_counter_handle_t counter, astl_counter_props_t* properties) const
    -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto*                 counter_details = static_cast<const CounterHandle*>(counter);
  if (!counter_details) {
    ASTL_LOG_ERROR("GetCounterProperties: Invalid counter handle {}", counter);
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto first_counter_instance = counter_details->target_to_counter_map.begin();
  if (first_counter_instance == counter_details->target_to_counter_map.end()) {
    ASTL_LOG_ERROR("GetCounterProperties: No counter config found for handle {}", counter);
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  first_counter_instance->second->GetProperties(properties);
  // ensure that the properties struct going out the API has a reference to this counter handle
  properties->handle = counter;
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Get the collection of collector operations needed to sample the given counter on the given target
 *
 * This method is called by the orchestrator to retrieve operations to send to CollectorManager
 *
 * @param counters A collection of counter API handles to collect
 * @param target A pointer to a target on which to collect samples for the given counters
 *
 * @return A CollectionOperations struct with operations for the CollectorManager to execute
 *         OR a status code indicating the nature of an error
 */
auto MetricManager::GetCounterRequiredOperations(std::span<const astl_counter_handle_t> counters, const ITarget* target)
    -> std::expected<CollectionOperations, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  /**
   * This method performs the following steps for each given counter:
   * - Validates each counter is registered (returns BAD_ARGUMENT if not).
   * - Ensures each counter uses an known collector (returns UNSUPPORTED_COLLECTOR_TYPE otherwise).
   * - For each given counter, asks for the sequence of operations needed to provide sample
   * - Records the operation_id to counter mapping for processing samples later.
   * - Returns the complete CollectionOperations struct or an appropriate error.
   * */

  OperationSequence op_sequence;

  if (target == nullptr) {
    ASTL_LOG_ERROR("GetCounterRequiredOperations: target is null");
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  // Reject empty requests up front instead of falling through to collector_type.value() below.
  if (counters.empty()) {
    ASTL_LOG_ERROR("GetCounterRequiredOperations: no counters requested");
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }

  std::optional<CollectorType> collector_type;

  for (const auto* counter_api_handle : counters) {
    const auto* counter_handle = static_cast<const CounterHandle*>(counter_api_handle);
    const auto& config         = counter_handle->config;
    if (auto iter = std::ranges::find_if(
            _counter_handles, [counter_handle](const auto& counter) { return counter.get() == counter_handle; });
        iter == _counter_handles.end()) {
      ASTL_LOG_ERROR("GetCounterRequiredOperations: Counter handle {} not registered", config->Name());
      return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
    }

    if (collector_type.has_value() && collector_type != config->GetCollectorType()) {
      ASTL_LOG_ERROR("GetCounterRequiredOperations: Mixed collector types in requested counters not supported");
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }
    collector_type = config->GetCollectorType();

    if (!IsCollectorTypeSupported(collector_type.value())) {
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }

    auto counter_iter = counter_handle->target_to_counter_map.find(target);
    if (counter_iter == counter_handle->target_to_counter_map.end()) {
      ASTL_LOG_ERROR("GetCounterRequiredOperations: Failed to get counter {} on target {}", config->Name(),
                     target->Name());
      return std::unexpected{ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET};
    }
    auto* counter = counter_iter->second.get();

    auto operations_result = counter->GetOperations();
    if (!operations_result.has_value()) {
      ASTL_LOG_ERROR("GetCounterRequiredOperations: Failed to get operations for residency counter '{}'",
                     config->Name());
      return std::unexpected{operations_result.error()};
    }
    auto counter_operations = std::move(operations_result.value());
    for (auto& operation : counter_operations) {
      uint32_t operation_id                                    = operation->GetId();
      _target_to_operation_to_metric_map[target][operation_id] = counter;
      op_sequence.push_back(std::move(operation));
      ASTL_LOG_INFO("GetRequiredOperations: Added operation from Counter::GetOperations() for counter '{}'",
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

auto MetricManager::SinkProcessedSamples(const IMetric* metric, std::span<const ProcessedSampledData> processed_samples)
    -> astl_status_code {
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
      if (residency_config->GetStateInfo().empty()) {
        ASTL_LOG_ERROR("CreateMetricFromConfig: No state info found in ResidencyMetricConfig for metric '{}'",
                       metric_name);
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      // Create state configurations from the metric config for the specific target
      std::vector<ResidencyMetricConfig::StateInfo> state_configs;
      // turn the map of state->info to a vector of StateInfo
      std::ranges::transform(
          residency_config->GetStateInfo(), std::back_inserter(state_configs), [](const auto& state_pair) {
            const auto& [state_name, state_data] = state_pair;
            return ResidencyMetricConfig::StateInfo{state_name, state_data.state_description, state_data.tick_frequency,
                                                    state_data.operation_builder};
          });
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
auto MetricManager::GetMetricOnTarget(astl_metric_handle_t metric_handle, const ITarget* target) const
    -> std::expected<IMetric*, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto*                 metric_details = static_cast<const MetricHandle*>(metric_handle);

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

auto MetricManager::RegisterProcessedSampleSink(IProcessedSampleSink* sink) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  _registered_processed_sample_sinks.insert(sink);
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::UnregisterProcessedSampleSink(IProcessedSampleSink* sink) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  if (!sink) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  // Unregistration is a no-op if the sink was not registered.
  _registered_processed_sample_sinks.erase(sink);
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::RegisterMetric(std::unique_ptr<MetricConfig>      metric_config,
                                   std::vector<const ITarget*> const& targets) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  ASTL_LOG_TRACE("RegisterMetric {} on {} targets", metric_config ? metric_config->Name() : "<null>", targets.size());
  if (!metric_config) {
    ASTL_LOG_ERROR("RegisterMetric: Invalid metric config");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (IsMetricIdRegistered(metric_config->Id())) {
    ASTL_LOG_ERROR("RegisterMetric: Duplicate metric id '{}'", metric_config->Id());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
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
    auto metric = std::move(metric_or_error.value());
    metric->SetProcessedSampleSink(this);
    target_specific_metrics[target] = std::move(metric);
  }
  auto* metric_config_ptr = metric_config.get();  // non-owning pointer. grab this before metric_config is moved
  _metric_handles.emplace_back(
      std::make_unique<MetricHandle>(std::move(metric_config), std::move(target_specific_metrics)));

  astl_metric_handle_t metric_handle = static_cast<astl_metric_handle_t>(_metric_handles.back().get());

  for (const auto* const target : targets) {
    _target_to_metrics_map[target].push_back(metric_handle);
  }

  auto status = AddMetricToGroups(metric_handle, metric_config_ptr, targets);

  return status;
}

auto MetricManager::GetNumAvailableMetrics(const ITarget* target) const -> size_t {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto                  target_iter = _target_to_metrics_map.find(target);
  if (target_iter == _target_to_metrics_map.end()) {
    ASTL_LOG_ERROR("GetNumAvailableMetrics: Target '{}' not found", target->Name());
    return 0;
  }
  return target_iter->second.size();
}

auto MetricManager::GetAvailableMetrics(const ITarget* target) const
    -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto                  target_iter = _target_to_metrics_map.find(target);
  if (target_iter == _target_to_metrics_map.end()) {
    std::string targets;
    for (const auto& target_metrics : _target_to_metrics_map) {
      targets.append(target_metrics.first->Name() + ", ");
    }
    ASTL_LOG_WARNING("GetAvailableMetrics: Target '{}' not found in '{}'.", target->Name(), targets);
    return {};
  }
  std::span<const astl_metric_handle_t> handles_span(target_iter->second);
  return std::expected<std::span<const astl_metric_handle_t>, astl_status_code>(std::in_place, handles_span);
}

/**
 * @brief Assign values such as name, units, etc to the given properties pointer.
 */
auto MetricManager::GetProperties(astl_metric_handle_t metric, astl_metric_props_t* properties) const
    -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto*                 metric_details = static_cast<const MetricHandle*>(metric);
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
  properties->handle = metric;  // the properties needs to be associated with the MetricHandle
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::GetRequiredOperations(std::span<const astl_metric_handle_t> metrics, const ITarget* target)
    -> std::expected<CollectionOperations, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  /**
   * This method performs the following steps for each given metric:
   * - Validates each metric is registered (returns BAD_ARGUMENT if not).
   * - Ensures each metric uses an known collector (returns UNSUPPORTED_COLLECTOR_TYPE otherwise).
   * - For each given metric, asks for the sequence of operations needed to provide sample
   * - Records the operation_id to metric mapping for processing samples later.
   * - Returns the complete CollectionOperations struct or an appropriate error.
   * */

  OperationSequence op_sequence;

  if (target == nullptr) {
    ASTL_LOG_ERROR("GetRequiredOperations: target is null");
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  // Reject empty requests up front instead of falling through to collector_type.value() below.
  if (metrics.empty()) {
    ASTL_LOG_ERROR("GetRequiredOperations: no metrics requested");
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }

  std::optional<CollectorType> collector_type;

  for (const auto* metric_api_handle : metrics) {
    const auto* metric_handle = static_cast<const MetricHandle*>(metric_api_handle);
    const auto& config        = metric_handle->config;

    if (auto iter = std::ranges::find_if(_metric_handles,
                                         [metric_handle](const auto& handle) { return handle.get() == metric_handle; });
        iter == _metric_handles.end()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Metric '{}' not registered", metric_api_handle);
      return std::unexpected{ASTL_STATUS_INVALID_METRIC_HANDLE};
    }

    if (collector_type.has_value() && collector_type != config->GetCollectorType()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Mixed collector types in requested metrics not supported");
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }
    collector_type = config->GetCollectorType();

    if (!IsCollectorTypeSupported(config->GetCollectorType())) {
      return std::unexpected{ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE};
    }

    auto metric_iter = metric_handle->target_to_metric_map.find(target);
    if (metric_iter == metric_handle->target_to_metric_map.end() || !metric_iter->second) {
      ASTL_LOG_ERROR("GetRequiredOperations: Failed to get metric {} on target {}", config->Name(), target->Name());
      return std::unexpected{ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET};
    }
    IMetric* metric = metric_iter->second.get();

    // NOTE: GetOperations() carries a constraint to run
    // single-threaded so that OperationIds are allocated in contiguous ascending order.
    // See comment in ResidencyMetric::GetOperations() for ordering contract relied upon when sinking
    // processed residency samples deterministically.
    auto operations_result = metric->GetOperations();
    if (!operations_result.has_value()) {
      ASTL_LOG_ERROR("GetRequiredOperations: Failed to get operations for residency metric '{}'", config->Name());
      return std::unexpected{operations_result.error()};
    }
    auto metric_operations = std::move(operations_result.value());
    for (auto& operation : metric_operations) {
      uint32_t operation_id                                    = operation->GetId();
      _target_to_operation_to_metric_map[target][operation_id] = metric;
      op_sequence.push_back(std::move(operation));
      ASTL_LOG_INFO("GetRequiredOperations: Added operation from IMetric::GetOperations() for metric '{}', op_id = {}",
                    config->Name(), operation_id);
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

auto MetricManager::ProcessRawSamples(RawSamplesMap& raw_samples) -> astl_status_code {
  std::vector<std::pair<IMetric*, RawSampledData>> processing_queue;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& [target, samples] : raw_samples) {
      if (!target) {
        ASTL_LOG_ERROR("ProcessRawSamples: Target is null");
        return ASTL_STATUS_BAD_ARGUMENT;
      }
      const auto target_iter = _target_to_operation_to_metric_map.find(target);
      if (target_iter == _target_to_operation_to_metric_map.end()) {
        ASTL_LOG_ERROR("ProcessRawSamples: No operation routing registered for target '{}'", target->Name());
        return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
      }
      for (const auto& sample : samples) {
        const auto op_iter = target_iter->second.find(sample.operation_id);
        if (op_iter == target_iter->second.end() || op_iter->second == nullptr) {
          ASTL_LOG_ERROR("ProcessRawSamples: No metric associated with operation ID {} on target '{}'",
                         sample.operation_id, target->Name());
          return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
        }
        processing_queue.emplace_back(op_iter->second, sample);
      }
    }
  }

  // Sort samples so that each metric receives its samples in non-decreasing timestamp order.
  // Primary key: metric pointer (groups all samples for the same metric together).
  // Secondary key: timestamp (ascending within each group).
  std::sort(processing_queue.begin(), processing_queue.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return std::less<IMetric*>{}(lhs.first, rhs.first);
    }
    return lhs.second.timestamp < rhs.second.timestamp;
  });

  for (const auto& [metric_handle, sample] : processing_queue) {
    astl_status_code status = metric_handle->ReceiveRawSample(sample);
    if (status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ProcessData: Failed to process sample for operation ID {} with status {}", sample.operation_id,
                     astlStatusString(status));
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::ResetMetricsOnTarget(const ITarget* target) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  if (target == nullptr) {
    ASTL_LOG_ERROR("ResetMetricsOnTarget: target is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  for (const auto& counter_handle : _counter_handles) {
    auto counter_it = counter_handle->target_to_counter_map.find(target);
    if (counter_it != counter_handle->target_to_counter_map.end() && counter_it->second) {
      counter_it->second->Reset();
    }
  }

  for (auto& metric_handle : _metric_handles) {
    auto metric_it = metric_handle->target_to_metric_map.find(target);
    if (metric_it != metric_handle->target_to_metric_map.end() && metric_it->second) {
      metric_it->second->Reset();
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::GetMetricGroups() const -> std::span<const astl_metric_group_handle_t> {
  std::lock_guard<std::mutex> lock(_mutex);
  return std::span<const astl_metric_group_handle_t>(_metric_group_api_handles);
}

auto MetricManager::GetMetricGroups(const ITarget* target) const
    -> std::expected<std::span<const astl_metric_group_handle_t>, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  if (target == nullptr) {
    ASTL_LOG_ERROR("GetMetricGroups: target is null");
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  if (_target_to_metric_groups_map.empty()) {
    ASTL_LOG_WARNING("GetMetricGroups: No metric groups registered in manager");
    return std::span<const astl_metric_group_handle_t>{};
  }
  const auto target_iter = _target_to_metric_groups_map.find(target);
  if (target_iter == _target_to_metric_groups_map.end()) {
    // A valid target may legitimately expose zero metric groups.
    return std::span<const astl_metric_group_handle_t>{};
  }
  std::span<const astl_metric_group_handle_t> handles_span(target_iter->second);
  return std::expected<std::span<const astl_metric_group_handle_t>, astl_status_code>(std::in_place, handles_span);
}

auto MetricManager::GetMetricGroupProperties(astl_metric_group_handle_t group,
                                             astl_metric_group_props_t* properties) const -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
  const auto*                 metric_group_details = static_cast<const MetricGroup*>(group);
  if (!metric_group_details) {
    ASTL_LOG_ERROR("GetMetricGroupProperties: Invalid metric group handle {}", group);
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  return metric_group_details->ToMetricGroupProperties(properties);
}

/**
 * @brief Retrieve the metric handles associated with a given metric group instance
 */
auto MetricManager::GetMetricsInGroup(astl_metric_group_handle_t group) const
    -> std::expected<std::span<const astl_metric_handle_t>, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
  if (group == nullptr) {
    ASTL_LOG_ERROR("GetMetricsInGroup: Invalid metric group handle {}", group);
    return std::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  }
  const MetricGroup* metric_group = MetricGroup::FromApiHandle(group);
  return std::span<const astl_metric_handle_t>{metric_group->metrics};
}

auto MetricManager::SummarizeMetrics() -> astl_status_code {
  std::lock_guard<std::mutex> lock(_mutex);
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

auto MetricManager::RemoveAllMetrics() -> void {
  std::lock_guard<std::mutex> lock(_mutex);
  _metric_handles.clear();
  _metric_groups.clear();
  _metric_group_api_handles.clear();
  _target_to_metrics_map.clear();
  _target_to_metric_groups_map.clear();
  _target_to_operation_to_metric_map.clear();
}

auto MetricManager::IsCollectorTypeSupported(CollectorType required_collector_type) const -> bool {
  // Check against the manager's capabilities.
  // NOTE: caller is responsible for synchronization when needed.
  const std::vector<CollectorCapability>& collector_caps = _capabilities.GetCollectorCapability();
  return std::any_of(collector_caps.begin(), collector_caps.end(),
                     [&](const CollectorCapability& cap) { return cap.GetCollectorType() == required_collector_type; });
}

auto MetricManager::IsMetricIdRegistered(const std::string& metric_id) const -> bool {
  return std::any_of(_metric_handles.begin(), _metric_handles.end(),
                     [&](const auto& handle) { return handle && handle->config && handle->config->Id() == metric_id; });
}

/**
 * @brief Helper for RegisterMetric to add metric to groups based on its config
 *        Will need to update a few member variables, including:
 *        _metric_groups
 *        _metric_group_api_handles
 *        _target_to_metric_groups_map
 * @param metric_handle The metric handle to add to groups
 * @param metric_config The metric config used to determine group membership
 * @param targets       The targets associated with this metric
 */
auto MetricManager::AddMetricToGroups(astl_metric_handle_t metric_handle, const MetricConfig* metric_config,
                                      const std::vector<const ITarget*>& targets) -> astl_status_code {
  ASTL_LOG_TRACE("AddMetricToGroups: Adding metric {} to {} metric groups on {} targets", metric_config->Name(),
                 metric_config->MetricGroups().size(), targets.size());
  for (const auto& group_name : metric_config->MetricGroups()) {
    astl_metric_group_handle_t group_handle{nullptr};
    // Check if the group already exists
    auto group_lookup =
        std::find_if(_metric_groups.begin(), _metric_groups.end(),
                     [&](const std::unique_ptr<MetricGroup>& group) { return group->name == group_name; });
    if (group_lookup == _metric_groups.end()) {
      std::string group_description;
      if (const auto description_iter = _metric_group_descriptions.find(group_name);
          description_iter != _metric_group_descriptions.end()) {
        group_description = description_iter->second;
      } else {
        ASTL_LOG_ERROR("AddMetricToGroups: Metric group '{}' is not defined in group metadata config", group_name);
        return ASTL_STATUS_BAD_CONFIGURATION;
      }
      // Create a new group
      auto new_group = std::make_unique<MetricGroup>(group_name, std::move(group_description),
                                                     std::vector<astl_metric_handle_t>{metric_handle});
      group_handle   = new_group->ToApiHandle();
      // register the new group
      _metric_groups.push_back(std::move(new_group));
      _metric_group_api_handles.push_back(group_handle);
    } else {
      group_handle = (*group_lookup)->ToApiHandle();
      // Group already exists, just add the metric to it
      (*group_lookup)->metrics.push_back(metric_handle);
    }
    // finally, make sure that the metric group is associated with each given target
    auto status = AddMetricGroupToTargets(group_handle, targets);
    ASTL_LOG_TRACE("AddMetricToGroups: -- Adding metric {} to {}: status: {}", metric_config->Name(), group_name,
                   status);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::AddMetricGroupToTargets(astl_metric_group_handle_t         group_handle,
                                            const std::vector<const ITarget*>& targets) -> astl_status_code {
  ASTL_LOG_TRACE("AddMetricGroupToTargets: Adding group handle {} to {} targets", group_handle, targets.size());
  for (const auto* target : targets) {
    auto target_and_groups = _target_to_metric_groups_map.find(target);
    if (target_and_groups == _target_to_metric_groups_map.end()) {
      _target_to_metric_groups_map[target] = std::vector<astl_metric_group_handle_t>{group_handle};
    } else {
      auto& groups_for_target = target_and_groups->second;
      if (std::find(groups_for_target.begin(), groups_for_target.end(), group_handle) == groups_for_target.end()) {
        groups_for_target.push_back(group_handle);
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto MetricManager::GetTargetForMetric(const IMetric* metric) const -> std::expected<const ITarget*, astl_status_code> {
  std::lock_guard<std::mutex> lock(_mutex);
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

auto MetricManager::SinkProcessedSamples(const ITarget* target, const IMetric* metric,
                                         std::span<const ProcessedSampledData> processed_samples) -> astl_status_code {
  // NOTE:
  // Sinks are invoked while holding _mutex.
  // RegisterProcessedSampleSink()/UnregisterProcessedSampleSink() must NOT be called from
  // within SinkProcessedSamples callbacks, directly or indirectly, or this can deadlock.
  std::lock_guard<std::mutex> lock(_mutex);
  astl_status_code            result = ASTL_STATUS_SUCCESS;
  for (auto* sink : _registered_processed_sample_sinks) {
    const auto sink_result = sink->SinkProcessedSamples(target, metric, processed_samples);
    if (sink_result != ASTL_STATUS_SUCCESS) {
      result = sink_result;  // record last failure and continue
    }
  }
  return result;
}
}  // namespace astl
