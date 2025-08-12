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
#include <numeric>

#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "scmi/scmi_read_operation.hpp"

namespace astl {

ResidencyMetric::ResidencyMetric(const char* name, const char* description,
                                 const std::vector<StateConfiguration>& state_configs,
                                 const std::optional<std::string>&      inferred_state_name)
    : DeltaMetric(name, description, ASTL_UNITS_TICKS, ASTL_VALUE_UINT64),
      _state_configs(state_configs),
      _inferred_state_name(inferred_state_name) {
  InitializeResidencyState();

  // Header initialization for residency summary logger
  _residency_summary_logger.LogInfo(
      "Metric, State, Total_Time_Seconds, Average_Percentage, Min_Percentage, Max_Percentage\n");
}

void ResidencyMetric::Reset() {
  DeltaMetric::Reset();
  InitializeResidencyState();
}

void ResidencyMetric::InitializeResidencyState() {
  _previous_samples.clear();
  _residency_data.clear();
  _summary_data = {};
  _state_time_totals.clear();
  _state_percentage_sums.clear();
  _state_sample_counts.clear();
  _operation_id_to_config.clear();
  _processed_states_per_timestamp.clear();

  // Initialize tracking for all configured states
  for (const auto& config : _state_configs) {
    _state_time_totals[config.state_name]           = std::chrono::duration<double>(0.0);
    _state_percentage_sums[config.state_name]       = 0.0;
    _state_sample_counts[config.state_name]         = 0;
    _summary_data.min_percentage[config.state_name] = std::numeric_limits<double>::max();
    _summary_data.max_percentage[config.state_name] = 0.0;
  }

  // Initialize tracking for inferred state if configured
  if (_inferred_state_name.has_value()) {
    _state_time_totals[_inferred_state_name.value()]           = std::chrono::duration<double>(0.0);
    _state_percentage_sums[_inferred_state_name.value()]       = 0.0;
    _state_sample_counts[_inferred_state_name.value()]         = 0;
    _summary_data.min_percentage[_inferred_state_name.value()] = std::numeric_limits<double>::max();
    _summary_data.max_percentage[_inferred_state_name.value()] = 0.0;
  }
}

std::expected<OperationSequence, astl_status_code> ResidencyMetric::GetOperations() {
  OperationSequence operations_seq;

  // Create an operation for each configured state
  for (const auto& state_config : _state_configs) {
    // Create SCMI read operation for this state's data event
    auto        operation = std::make_unique<ScmiReadOperation>(state_config.data_event_id);
    OperationId op_id     = operation->GetId();
    operations_seq.push_back(std::move(operation));
    // Build the operation_id to config map for fast lookup
    _operation_id_to_config[op_id] = &state_config;

    ASTL_LOG_DEBUG("ResidencyMetric: Created operation for state '{}' with data_event_id '{}'", state_config.state_name,
                   state_config.data_event_id);
  }

  ASTL_LOG_INFO("ResidencyMetric: Created {} operations for metric '{}'", operations_seq.size(), _name);

  return operations_seq;
}

astl_status_code ResidencyMetric::ReceiveSample(const SampledData& sample) {
  // Find the state configuration for this sample's operation_id using fast map lookup
  auto config_it = _operation_id_to_config.find(sample.operation_id);
  if (config_it == _operation_id_to_config.end()) {
    ASTL_LOG_ERROR("ResidencyMetric: No state configuration found for operation_id {}", sample.operation_id);
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  const StateConfiguration* state_config = config_it->second;

  // @todo: Currently the inferred state residency is only computed in the Summarize method.
  // If the state is inferred, we skip processing it in each sampling interval.
  // This is because inferred states are derived from other states and do not have direct samples.
  // Each sampling interval we need to ensure the samples for other states are received before we can compute the
  // inferred state.

  // Check if the sample's value type matches the metric's expected type
  auto type_check_result = CheckSampleValueType(sample);
  if (type_check_result != ASTL_STATUS_SUCCESS) {
    return type_check_result;
  }

  // Log the raw sample using the base class method
  LogRawSample(sample);

  const std::string& state_name = state_config->state_name;

  // If this is the first sample for this state, store it and return
  if (_previous_samples[state_name] == std::nullopt) {
    _previous_samples[state_name] = sample;
    return ASTL_STATUS_SUCCESS;
  }

  // Calculate delta between current and previous sample for this state
  auto delta_result = CalculateDelta(sample.value, _previous_samples[state_name]->value);
  if (!delta_result.has_value()) {
    ASTL_LOG_ERROR("ResidencyMetric: failed to calculate delta for state {} in metric {}: {}", state_name,
                   _name.c_str(), astlStatusString(delta_result.error()));
    return delta_result.error();
  }

  // Convert delta ticks to time in microseconds
  auto time_result = ConvertTicksToMicroseconds(delta_result.value(), *state_config);
  if (!time_result.has_value()) {
    ASTL_LOG_ERROR("ResidencyMetric: failed to convert ticks to microseconds for state {} in metric {}: {}", state_name,
                   _name.c_str(), astlStatusString(time_result.error()));
    return time_result.error();
  }

  // Calculate time interval between samples
  auto time_interval = std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp -
                                                                             _previous_samples[state_name]->timestamp);

  // Calculate percentage residency
  auto percentage_result = CalculatePercentage(time_result.value(), time_interval);
  if (!percentage_result.has_value()) {
    ASTL_LOG_ERROR("ResidencyMetric: failed to calculate percentage for state {} in metric {}: {}", state_name,
                   _name.c_str(), astlStatusString(percentage_result.error()));
    return percentage_result.error();
  }

  // Update state residency statistics
  auto status =
      UpdateStateResidencyStatistics(state_name, time_result.value(), percentage_result.value(), sample.timestamp);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  // Calculate inferred state residency if configured and all states have been processed for this timestamp
  if (_inferred_state_name.has_value() && (_state_configs.size() == _processed_states_per_timestamp.size())) {
    auto inferred_status = CalculateInferredStateResidencyForInterval(time_interval, sample.timestamp);
    if (inferred_status != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ResidencyMetric: failed to calculate inferred state residency for metric {}: {}", _name.c_str(),
                     astlStatusString(inferred_status));
      return inferred_status;
    }

    // Clear the tracking set since we've processed all states for this timestamp
    _processed_states_per_timestamp.clear();
  }

  // Store current sample as previous for next iteration for this state
  _previous_samples[state_name] = sample;

  return ASTL_STATUS_SUCCESS;
}

astl_status_code ResidencyMetric::Summarize() {
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

    _residency_summary_logger.LogInfo("{}, {}, {:.6f}, {:.2f}, {:.2f}, {:.2f}\n", _name, state_name, total_time.count(),
                                      avg_percentage, min_percentage, max_percentage);
  }

  // Log inferred state if present
  if (_summary_data.inferred_state_percentage.has_value() && _inferred_state_name.has_value()) {
    _residency_summary_logger.LogInfo(
        "{}, {}, {:.6f}, {:.2f}, {:.2f}, {:.2f}\n", _name, _inferred_state_name.value(),
        _summary_data.inferred_state_time.value_or(0.0), _summary_data.inferred_state_percentage.value(),
        _summary_data.inferred_state_percentage.value(), _summary_data.inferred_state_percentage.value());
  }

  return ASTL_STATUS_SUCCESS;
}

const ResidencySummaryData& ResidencyMetric::GetResidencySummaryData() const { return _summary_data; }

std::span<const StateResidencyData> ResidencyMetric::GetResidencyData() const { return _residency_data; }

std::vector<StateResidencyData> ResidencyMetric::GetStateResidencyData(const std::string& state_name) const {
  std::vector<StateResidencyData> state_data;
  std::copy_if(_residency_data.begin(), _residency_data.end(), std::back_inserter(state_data),
               [&state_name](const StateResidencyData& data) { return data.state_name == state_name; });
  return state_data;
}

std::expected<std::chrono::microseconds, astl_status_code> ResidencyMetric::ConvertTicksToMicroseconds(
    const AstlValue& ticks, const StateConfiguration& config) {
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

std::expected<double, astl_status_code> ResidencyMetric::CalculatePercentage(std::chrono::microseconds time_in_state,
                                                                             std::chrono::microseconds total_interval) {
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

astl_status_code ResidencyMetric::UpdateStateResidencyStatistics(const std::string&        state_name,
                                                                 std::chrono::microseconds time_microseconds,
                                                                 double percentage, SampleTimestamp timestamp) {
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

astl_status_code ResidencyMetric::CalculateInferredStateResidencyForInterval(std::chrono::microseconds sample_interval,
                                                                             SampleTimestamp           timestamp) {
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
        accounted_time.count(), sample_interval.count(), _name.c_str());
  }

  // Calculate inferred state time (time not accounted for in this interval)
  using rep = std::chrono::microseconds::rep;
  auto inferred_time_microseconds =
      std::chrono::microseconds(std::max(sample_interval.count() - accounted_time.count(), static_cast<rep>(0)));
  double inferred_percentage =
      (static_cast<double>(inferred_time_microseconds.count()) / static_cast<double>(sample_interval.count())) * 100.0;

  // Clamp percentage to [0, 100] range
  inferred_percentage = std::clamp(inferred_percentage, 0.0, 100.0);

  // Create StateResidencyData for the inferred state
  if (inferred_time_microseconds.count() > 0) {
    AstlValue inferred_ticks{uint64_t{0}};  // Inferred state doesn't have raw tick data

    // Use UpdateStateResidencyStatistics to update the statistics and store the inferred state data
    auto status = UpdateStateResidencyStatistics(_inferred_state_name.value(), inferred_time_microseconds,
                                                 inferred_percentage, timestamp);
    if (status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }

  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
