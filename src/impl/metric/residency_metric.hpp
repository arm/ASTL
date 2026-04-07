// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef RESIDENCY_METRIC_HPP_
#define RESIDENCY_METRIC_HPP_

#include <chrono>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "common/metric_config.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "delta_metric.hpp"

namespace astl {

/**
 * @brief Holds residency calculation data for a specific state.
 * note @todo (ASTL-159): Revisit the Residency samples data structure for GetProcessedSamples API
 * This structure stores the residency delta, converted time, and percentage for each state and string_name can be
 * optimized with a map.
 */
struct StateResidencyData {
  std::string                   state_name;    ///< Name of the power/system state
  std::chrono::duration<double> time_seconds;  ///< Converted time in seconds
  ProcessedSampleTimestamp      timestamp;     ///< Timestamp when calculated
};

/**
 * @brief Holds summary data for all states in the residency metric.
 * This structure stores accumulated residency statistics per state.
 * note @todo // TODO (ASTL-58): When the output manager is implemented revisit  this datastruct as  a small struct for
 * time_stats that holds total/avg/min/max, and a single unordered_map<string, time_stats>
 */
struct ResidencySummaryData {
  std::unordered_map<std::string, std::chrono::duration<double>>
                                          total_time_seconds;         ///< Total time per state in seconds
  std::unordered_map<std::string, double> average_percentage;         ///< Average percentage per state
  std::unordered_map<std::string, double> min_percentage;             ///< Minimum percentage per state
  std::unordered_map<std::string, double> max_percentage;             ///< Maximum percentage per state
  std::optional<double>                   inferred_state_percentage;  ///< Percentage for inferred state
  std::optional<double>                   inferred_state_time;        ///< Total time for inferred state
};

/**
 * @brief Residency metric class for handling state residency metrics.
 *
 * ## Residency Metric Concept
 *
 * A residency metric measures the time spent in different system states by tracking
 * hardware counters that increment while the system is in those states. This is
 * particularly useful for power management analysis and performance optimization.
 *
 * ### Example: CPU C-State Residency Measurement
 *
 * Consider a CPU with the following power management states:
 * - **C0 (Active)**: CPU executing instructions, all clocks running, full power
 * - **C1 (Clock Gated)**: CPU execution halted, clocks stopped, quick wake-up (~1μs)
 * - **C6 (Power Gated)**: CPU powered down, voltage reduced, longer wake-up (~100μs)
 *
 * Each state has a hardware counter that increments at a known frequency while the CPU is in that state and may or may
 * not have an inferred state when CPU is not in any known state.
 *
 * ResidencyMetric inherits from DeltaMetric and adds the ability to:
 * - Track multiple states (C-states, P-states, etc.)
 * - Calculate residency deltas for each state
 * - Convert ticks to time units
 * - Calculate percentage time spent in each state
 * - Handle inferred states (time not accounted for by known states)
 * - Generate comprehensive residency summaries
 *
 * Other Examples: P-state residency, GPU state residency
 */
class ResidencyMetric : public DeltaMetric {
 public:
  ResidencyMetric() = delete;

  /**
   * @brief Construct a ResidencyMetric with specified name, description, and state configurations.
   *
   * Initializes the metric with the provided parameters and sets up state tracking.
   * The metric will track residency for all configured states.
   *
   * @param configuration The name, description, units, representation, inferred state, and operation builder of this
   * metric
   * @param state_configs Vector of state configurations defining the states to track.
   * @param target The telemetry source for this metric instance
   * @param processed_sample_sink Output for where processed samples should be sent
   */
  explicit ResidencyMetric(const ResidencyMetricConfig*                  configuration,
                           std::vector<ResidencyMetricConfig::StateInfo> state_configs, const ITarget* target,
                           IProcessedSampleSink* processed_sample_sink);

  /**
   * @brief Reset the metric state, dropping all collected samples and residency data.
   */
  void Reset() override;

  /**
   * @brief Handle a pause event by resetting all per-state previous-sample references.
   *
   * @param pause_timestamp CLOCK_MONOTONIC_RAW timestamp of the pause event.
   * @return ASTL_STATUS_SUCCESS.
   */
  auto ProcessPauseSample(ProcessedSampleTimestamp pause_timestamp) -> astl_status_code override;

  /**
   * @brief Process and record a new sample value for a specific state.
   *
   * Calculates residency delta for the specific state identified by the sample's operation_id,
   * converts ticks to time, and calculates percentage residency.
   *
   * @param sample A single sampled data point containing state counter value.
   * @return astl_status_code indicating success or failure.
   */
  auto ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code override;

  /**
   * @brief Summarize collected residency data for all states.
   *
   * Finalizes the summary by calculating total time, average percentages,
   * and inferred state residency. Logs comprehensive residency statistics.
   *
   * @return astl_status_code indicating success or failure.
   */
  auto Summarize() -> astl_status_code override;

  /**
   * @brief Get the Operations required for this residency metric.
   *
   * Creates read operations for each configured state. Each state configuration
   * corresponds to one operation that will be used to collect counter data for that state.
   *
   * @return OperationSequence containing operations for all states, or error code on failure.
   */
  std::expected<OperationSequence, astl_status_code> GetOperations() override;

  /**
   * @brief Retrieve the complete residency summary data.
   *
   * Returns the current residency summary data containing statistics for all
   * tracked states including totals, averages, and inferred state data.
   *
   * @return A const reference to ResidencySummaryData struct with complete residency statistics.
   */
  const ResidencySummaryData& GetResidencySummaryData() const;

  /**
   * @brief Get a view of the residency data calculated by this metric.
   *
   * This method provides access to the internal residency data for all states.
   *
   * note @todo (ASTL-159): Implement a GetProcessedSamples API in all Metric types to get a span of processed samples.
   *
   * @return A span containing all calculated residency values for all states.
   */
  std::span<const StateResidencyData> GetResidencyData() const;

  /**
   * @brief Get residency data for a specific state.
   *
   * @param state_name The name of the state to retrieve data for.
   * @return Vector of residency data for the specified state.
   */
  std::vector<StateResidencyData> GetStateResidencyData(const std::string& state_name) const;

  /**
   * @brief Get the state configurations.
   * @return Const reference to the vector of state configurations.
   */
  const std::vector<ResidencyMetricConfig::StateInfo>& GetStateConfigs() const { return _state_configs; }

  /**
   * @brief Get the residency metric configuration.
   * @return Pointer to the ResidencyMetricConfig.
   */
  const ResidencyMetricConfig* GetResidencyConfiguration() const { return _residency_configuration; }

 protected:
  /**
   * @brief Initialize/reset residency samples and summary data.
   *
   * Resets the metric state by clearing all residency data and reinitializing
   * the summary data structures for all configured states.
   */
  void InitializeResidencyState();

  /**
   * @brief Convert ticks to microseconds using the configured frequency for a state.
   *
   * @param ticks The tick count to convert.
   * @param config The state configuration containing the tick frequency.
   * @return Expected time as std::chrono::microseconds or error code.
   */
  static std::expected<std::chrono::microseconds, astl_status_code> ConvertTicksToMicroseconds(
      const AstlValue& ticks, const ResidencyMetricConfig::StateInfo& config);

  /**
   * @brief Calculate percentage residency from time spent in state and total interval.
   *
   * @param time_in_state Time spent in the state (std::chrono::microseconds).
   * @param total_interval Total time interval between samples (microseconds).
   * @return Expected percentage value or error code.
   */
  static std::expected<double, astl_status_code> CalculatePercentage(std::chrono::microseconds time_in_state,
                                                                     std::chrono::microseconds total_interval);

  /**
   * @brief Update residency statistics for a specific state.
   *
   * @param state_name The name of the state.
   * @param time_microseconds Converted time in microseconds.
   * @param percentage Calculated percentage.
   * @param timestamp Sample timestamp.
   * @return astl_status_code indicating success or failure.
   */
  auto UpdateStateResidencyStatistics(const std::string& state_name, std::chrono::microseconds time_microseconds,
                                      double percentage, ProcessedSampleTimestamp timestamp) -> astl_status_code;

  /**
   * @brief Calculate and update inferred state residency for a specific sample interval.
   *
   * Calculates the residency of the inferred state for the current sample interval,
   * creating a StateResidencyData entry for the inferred state.
   *
   * @param sample_interval Time interval for this sample.
   * @param timestamp Timestamp for this sample.
   * @return astl_status_code indicating success or failure.
   */
  auto CalculateInferredStateResidencyForInterval(std::chrono::microseconds sample_interval,
                                                  ProcessedSampleTimestamp  timestamp) -> astl_status_code;

 private:
  // non-owned pointer to the configuration for this metric (owned by the MetricHandle)
  const ResidencyMetricConfig*                  _residency_configuration;
  std::vector<ResidencyMetricConfig::StateInfo> _state_configs;  ///< Configuration for all states

  std::unordered_map<OperationId, const ResidencyMetricConfig::StateInfo*>
      _operation_id_to_config;  ///< Fast lookup map from operation_id to state config
  // First (smallest) operation id assigned (captures ordering baseline). Set when operations are built.
  std::optional<OperationId> _first_operation_id;
  std::unordered_map<std::string, std::optional<NormalizedSampledData>>
                                  _previous_samples;  ///< Previous samples per state
  std::vector<StateResidencyData> _residency_data;    ///< All residency calculations
  ResidencySummaryData            _summary_data;      ///< Summary statistics

  // Per-state running totals for summary calculation
  std::unordered_map<std::string, std::chrono::duration<double>> _state_time_totals;  ///< Running total time per state
  std::unordered_map<std::string, double> _state_percentage_sums;  ///< Running percentage sum per state
  std::unordered_map<std::string, size_t> _state_sample_counts;    ///< Sample count per state

  // Tracking for inferred state calculation
  std::unordered_map<std::string, std::chrono::microseconds>
      _processed_states_per_timestamp;  ///< Track states and their time intervals for each timestamp

  // Pending processed samples keyed by OperationId (raw sample source). Inferred state (derived) stored separately.
  std::unordered_map<OperationId, ProcessedSampledData>
                                      _pending_processed_samples;  ///< Collected samples awaiting ordered sink
  std::optional<ProcessedSampledData> _pending_inferred_sample;    ///< Inferred sample (if any) for interval

  // Sink all pending processed samples in the order of _state_configs then inferred state (if any)
  astl_status_code SinkOrderedStateSamples();

  // Logger for residency summaries
  astl::Logger _residency_summary_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                                         false /* No default formatting */, "residency_summary.log"};
};

}  // namespace astl

#endif  // RESIDENCY_METRIC_HPP_
