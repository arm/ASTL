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

#include "residency_metric.hpp"

#include <algorithm>
#include <format>

#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace astl {

ResidencyMetric::ResidencyMetric(const ResidencyMetricConfig*                  configuration,
                                 std::vector<ResidencyMetricConfig::StateInfo> state_configs, const ITarget* target,
                                 IProcessedSampleSink* processed_sample_sink)
    : DeltaMetric{configuration, target, processed_sample_sink},
      _residency_configuration{configuration},
      _state_configs{std::move(state_configs)} {
  InitializeResidencyState();

  // Header initialization for residency summary logger
  _residency_summary_logger.LogInfo(
      "Metric, State, Total_Time_Seconds, Average_Percentage, Min_Percentage, Max_Percentage\n");
}

auto ResidencyMetric::Reset() -> void { InitializeResidencyState(); }

auto ResidencyMetric::InitializeResidencyState() -> void {
  _previous_samples.clear();
  _residency_data.clear();
  _summary_data = {};
  _state_time_totals.clear();
  _state_percentage_sums.clear();
  _state_sample_counts.clear();
  _operation_id_to_config.clear();
  _processed_states_per_timestamp.clear();
  _first_operation_id.reset();

  // Initialize tracking for all configured states
  for (const auto& config : _state_configs) {
    _state_time_totals[config.state_name]           = std::chrono::duration<double>(0.0);
    _state_percentage_sums[config.state_name]       = 0.0;
    _state_sample_counts[config.state_name]         = 0;
    _summary_data.min_percentage[config.state_name] = std::numeric_limits<double>::max();
    _summary_data.max_percentage[config.state_name] = 0.0;
  }

  // Initialize tracking for inferred state if configured
  if (const auto& inferred_state = _residency_configuration->InferredState().value_or(""); !inferred_state.empty()) {
    _state_time_totals[inferred_state]           = std::chrono::duration<double>(0.0);
    _state_percentage_sums[inferred_state]       = 0.0;
    _state_sample_counts[inferred_state]         = 0;
    _summary_data.min_percentage[inferred_state] = std::numeric_limits<double>::max();
    _summary_data.max_percentage[inferred_state] = 0.0;
  }
}

// NOTE: Ordering Contract
// This method MUST NOT be invoked concurrently from multiple threads for the different Metric instance.
// ResidencyMetric relies on OperationIds being assigned in strictly increasing, contiguous order corresponding
// to the sequence of configured states. That invariant is captured by storing the first OperationId and then
// later assuming a dense block [first, first + N) when sinking pending processed samples (see SinkOrderedStateSamples).
// If GetOperations were to run in parallel with itself (or interleaved with other Operation creation affecting
// global id assignment), the contiguity or relative ordering of OperationIds could be broken, leading to
// non‑deterministic sink ordering. Keep this single‑threaded or introduce stronger ordering guarantees in the
// OperationId allocator before removing this constraint.
auto ResidencyMetric::GetOperations() -> std::expected<OperationSequence, astl_status_code> {
  OperationSequence operations_seq;

  // Create an operation for each configured state
  for (const auto& state_config : _state_configs) {
    auto operations = BuildOperations(state_config.operation_builder, _target);
    if (!operations.has_value()) {
      ASTL_LOG_ERROR("ResidencyMetric: Failed to build operations for state '{}' in metric '{}': {}",
                     state_config.state_name, _configuration->Name(), astlStatusString(operations.error()));
      return std::unexpected(operations.error());
    }
    for (auto& operation : operations.value()) {
      OperationId op_id = operation->GetId();
      if (!_first_operation_id.has_value()) {
        _first_operation_id = op_id;  // capture baseline for contiguous ordering
      }
      operations_seq.push_back(std::move(operation));
      // Build the operation_id to config map for fast lookup
      _operation_id_to_config[op_id] = &state_config;

      ASTL_LOG_DEBUG("ResidencyMetric: Created operation for state '{}' with operation_id '{}' in metric '{}'",
                     state_config.state_name, op_id, _configuration->Name());
    }
  }

  ASTL_LOG_INFO("ResidencyMetric: Created {} operations for metric '{}'", operations_seq.size(),
                _configuration->Name());

  return operations_seq;
}

auto ResidencyMetric::ReceiveRawSample(const RawSampledData& raw_sample) -> astl_status_code {
  // Find the state configuration for this sample's operation_id using fast map lookup
  auto config_it = _operation_id_to_config.find(raw_sample.operation_id);
  if (config_it == _operation_id_to_config.end()) {
    ASTL_LOG_ERROR("ResidencyMetric: No state configuration found for operation_id {}", raw_sample.operation_id);
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  const auto& state_config = config_it->second;

  // @todo: Currently the inferred state residency is only computed in the Summarize method.
  // If the state is inferred, we skip processing it in each sampling interval.
  // This is because inferred states are derived from other states and do not have direct samples.
  // Each sampling interval we need to ensure the samples for other states are received before we can compute the
  // inferred state.

  // Check if the raw sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(raw_sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the raw sample using the base class method
  LogRawSample(raw_sample);

  const std::string& state_name = state_config->state_name;

  // If this is the first sample for this state, store it and return
  if (_previous_samples[state_name] == std::nullopt) {
    _previous_samples[state_name] = raw_sample;
    return ASTL_STATUS_SUCCESS;
  }

  // Calculate delta between current and previous sample for this state
  auto delta_result = CalculateDelta(raw_sample.value, _previous_samples[state_name]->value);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR("ResidencyMetric: failed to calculate delta for state {} in metric {}: {}", state_name,
                   _configuration->Name(), astlStatusString(delta_result.error()));
    return delta_result.error();
  }

  // Convert delta ticks to time in microseconds
  auto time_result = ConvertTicksToMicroseconds(delta_result.value(), *state_config);
  if (!time_result.has_value()) {
    ASTL_LOG_ERROR("ResidencyMetric: failed to convert ticks to microseconds for state {} in metric {}: {}", state_name,
                   _configuration->Name(), astlStatusString(time_result.error()));
    return time_result.error();
  }
  // Collect processed sample (do not sink yet; enforce ordering across states later)
  // Use insert_or_assign to avoid default-constructing ProcessedSampledData (no default ctor)
  _pending_processed_samples.insert_or_assign(
      raw_sample.operation_id,
      ProcessedSampledData{AstlValue{static_cast<uint64_t>(time_result.value().count())}, raw_sample.timestamp});

  // Calculate time interval between samples
  auto time_interval = std::chrono::duration_cast<std::chrono::microseconds>(raw_sample.timestamp -
                                                                             _previous_samples[state_name]->timestamp);

  // Calculate percentage residency
  auto percentage_result = CalculatePercentage(time_result.value(), time_interval);
  if (!percentage_result.has_value()) {
    ASTL_LOG_ERROR("ResidencyMetric: failed to calculate percentage for state {} in metric {}: {}", state_name,
                   _configuration->Name(), astlStatusString(percentage_result.error()));
    return percentage_result.error();
  }

  // Update state residency statistics
  auto status =
      UpdateStateResidencyStatistics(state_name, time_result.value(), percentage_result.value(), raw_sample.timestamp);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Calculate inferred state residency if configured and all states have been processed for this timestamp
  if (_residency_configuration->InferredState().has_value() &&
      (_state_configs.size() == _processed_states_per_timestamp.size())) {
    auto inferred_status = CalculateInferredStateResidencyForInterval(time_interval, raw_sample.timestamp);
    if (inferred_status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ResidencyMetric: failed to calculate inferred state residency for metric {}: {}",
                     _configuration->Name(), astlStatusString(inferred_status));
      return inferred_status;
    }

    // Now sink all pending samples in deterministic order
    auto sink_status = SinkOrderedStateSamples();
    if (sink_status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ResidencyMetric: failed to sink ordered processed samples for metric {}: {}",
                     _configuration->Name(), astlStatusString(sink_status));
      return sink_status;
    }

    // Clear the tracking set since we've processed all states for this timestamp
    _processed_states_per_timestamp.clear();
    _pending_processed_samples.clear();
    _pending_inferred_sample.reset();
  }

  // Store current sample as previous for next iteration for this state
  _previous_samples[state_name] = raw_sample;

  return ASTL_STATUS_SUCCESS;
}

auto ResidencyMetric::Summarize() -> astl_status_code {
  // Calculate final averages and populate summary data
  for (const auto& [state_name, sample_count] : _state_sample_counts) {
    if (sample_count > 0) {
      _summary_data.average_percentage[state_name] =
          _state_percentage_sums[state_name] / static_cast<double>(sample_count);
      _summary_data.total_time_seconds[state_name] = _state_time_totals[state_name];
    }
  }

  // Log summary data
  for (const auto& [state_name, total_time] : _summary_data.total_time_seconds) {
    auto avg_percentage = _summary_data.average_percentage[state_name];
    auto min_percentage = _summary_data.min_percentage[state_name];
    auto max_percentage = _summary_data.max_percentage[state_name];

    _residency_summary_logger.LogInfo("{}, {}, {:.6f}, {:.2f}, {:.2f}, {:.2f}\n", _configuration->Name(), state_name,
                                      total_time.count(), avg_percentage, min_percentage, max_percentage);
  }

  // Log inferred state if present
  if (_summary_data.inferred_state_percentage.has_value() && _residency_configuration->InferredState().has_value()) {
    _residency_summary_logger.LogInfo(
        "{}, {}, {:.6f}, {:.2f}, {:.2f}, {:.2f}\n", _configuration->Name(),
        _residency_configuration->InferredState().value(), _summary_data.inferred_state_time.value_or(0.0),
        _summary_data.inferred_state_percentage.value(), _summary_data.inferred_state_percentage.value(),
        _summary_data.inferred_state_percentage.value());
  }

  return ASTL_STATUS_SUCCESS;
}

auto ResidencyMetric::GetResidencySummaryData() const -> const ResidencySummaryData& { return _summary_data; }

auto ResidencyMetric::GetResidencyData() const -> std::span<const StateResidencyData> {
  return std::span<const StateResidencyData>(_residency_data);
}

auto ResidencyMetric::GetStateResidencyData(const std::string& state_name) const -> std::vector<StateResidencyData> {
  std::vector<StateResidencyData> state_data;
  std::copy_if(_residency_data.begin(), _residency_data.end(), std::back_inserter(state_data),
               [&state_name](const StateResidencyData& data) { return data.state_name == state_name; });
  return state_data;
}

auto ResidencyMetric::ConvertTicksToMicroseconds(const AstlValue& ticks, const ResidencyMetricConfig::StateInfo& config)
    -> std::expected<std::chrono::microseconds, astl_status_code> {
  if (config.tick_frequency <= 0.0) {
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }

  // Convert ticks to microseconds: time = (ticks / frequency) * 1,000,000
  // Use std::visit to extract the numeric value from AstlValue
  double ticks_value = std::visit(
      [](auto&& arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_arithmetic_v<T>) {
          return static_cast<double>(arg);
        } else {
          return 0.0;  // fallback for non-arithmetic types
        }
      },
      ticks.value);

  // Calculate time in microseconds with high precision
  double time_microseconds_double = (ticks_value / config.tick_frequency) * std::chrono::microseconds::period::den;

  // Convert to std::chrono::microseconds
  auto time_microseconds = std::chrono::microseconds(static_cast<int64_t>(std::round(time_microseconds_double)));

  return time_microseconds;
}

auto ResidencyMetric::CalculatePercentage(std::chrono::microseconds time_in_state,
                                          std::chrono::microseconds total_interval)
    -> std::expected<double, astl_status_code> {
  if (total_interval.count() <= 0) {
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }

  if (time_in_state.count() < 0) {
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }

  // Calculate percentage: (time_in_state / total_interval) * 100
  // Both values are in microseconds, so the division is precise
  double percentage =
      (static_cast<double>(time_in_state.count()) / static_cast<double>(total_interval.count())) * 100.0;

  // Clamp percentage to [0, 100] range
  percentage = std::clamp(percentage, 0.0, 100.0);

  return percentage;
}

auto ResidencyMetric::UpdateStateResidencyStatistics(const std::string&        state_name,
                                                     std::chrono::microseconds time_microseconds, double percentage,
                                                     SampleTimestamp timestamp) -> astl_status_code {
  // Convert time_microseconds to seconds as std::chrono::duration<double>
  std::chrono::duration<double> time_seconds_chrono = time_microseconds;

  // Create and store residency data entry
  StateResidencyData residency_data{
      .state_name = state_name, .time_seconds = time_seconds_chrono, .timestamp = timestamp};

  _residency_data.push_back(residency_data);

  // Update running totals and statistics
  _state_time_totals[state_name] += time_seconds_chrono;
  _state_percentage_sums[state_name] += percentage;
  _state_sample_counts[state_name]++;

  // Update min/max percentage tracking
  _summary_data.min_percentage[state_name] = std::min(_summary_data.min_percentage[state_name], percentage);
  _summary_data.max_percentage[state_name] = std::max(_summary_data.max_percentage[state_name], percentage);

  // Track this state's time for inferred state calculation
  _processed_states_per_timestamp[state_name] = time_microseconds;

  return ASTL_STATUS_SUCCESS;
}

auto ResidencyMetric::CalculateInferredStateResidencyForInterval(std::chrono::microseconds sample_interval,
                                                                 SampleTimestamp timestamp) -> astl_status_code {
  if (sample_interval.count() <= 0) {
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  // Calculate time accounted for by all known states in this interval
  // Sum up the time intervals from the processed states (all in microseconds)
  std::chrono::microseconds accounted_time{0};

  // Use the processed states map to get the time intervals and calculate accounted time
  for (const auto& [state_name, state_time_interval] : _processed_states_per_timestamp) {
    accounted_time += state_time_interval;
  }

  // Check for consistency - accounted time should not exceed total time
  if (accounted_time > sample_interval) {
    ASTL_LOG_CRITICAL(
        "ResidencyMetric: Accounted time ({} μs) exceeds total interval time ({} μs) for metric '{}' - this "
        "indicates a calculation error",
        accounted_time.count(), sample_interval.count(), _configuration->Name());
  }

  // Calculate inferred state time (time not accounted for in this interval)
  using rep = std::chrono::microseconds::rep;
  auto inferred_time_microseconds =
      std::chrono::microseconds(std::max(sample_interval.count() - accounted_time.count(), static_cast<rep>(0)));
  double inferred_percentage =
      (static_cast<double>(inferred_time_microseconds.count()) / static_cast<double>(sample_interval.count())) * 100.0;

  // Clamp percentage to [0, 100] range
  inferred_percentage = std::clamp(inferred_percentage, 0.0, 100.0);

  // Create StateResidencyData for the inferred state and capture time for pending sink ordering
  if (inferred_time_microseconds.count() > 0) {
    auto status = UpdateStateResidencyStatistics(_residency_configuration->InferredState().value(),
                                                 inferred_time_microseconds, inferred_percentage, timestamp);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
    // Also push a processed sample immediately (collected for ordered sink)
    _pending_inferred_sample =
        ProcessedSampledData{AstlValue{static_cast<uint64_t>(inferred_time_microseconds.count())}, timestamp};
  }

  return ASTL_STATUS_SUCCESS;
}

auto ResidencyMetric::SinkOrderedStateSamples() -> astl_status_code {
  // Assumption: OperationIds are sequential, contiguous, and assigned in configuration order.
  // Therefore, we can sink by iterating from the smallest id for count entries.
  if (!_pending_processed_samples.empty() && _first_operation_id.has_value()) {
    OperationId  base  = _first_operation_id.value();
    const size_t count = _pending_processed_samples.size();
    for (OperationId op = base, end = static_cast<OperationId>(base + count); op < end; ++op) {
      auto it_sample = _pending_processed_samples.find(op);
      // The contiguous OperationId invariant guarantees the element exists; use a descriptive name for clarity.
      const auto& processed_sample = it_sample->second;
      auto        status           = SinkProcessedSample(processed_sample);
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
    }
  }
  // Inferred sample last (if present)
  if (_pending_inferred_sample.has_value()) {
    auto status = SinkProcessedSample(_pending_inferred_sample.value());
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto ResidencyMetric::GetOrderedStates() const -> std::vector<std::string> {
  std::vector<std::string> order;
  order.reserve(_state_configs.size() + (_residency_configuration->InferredState().has_value() ? 1 : 0));
  std::transform(_state_configs.begin(), _state_configs.end(), std::back_inserter(order),
                 [](const auto& cfg) { return cfg.state_name; });
  if (_residency_configuration->InferredState().has_value()) {
    order.push_back(_residency_configuration->InferredState().value());
  }
  return order;
}

}  // namespace astl
