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

#ifndef METRIC_CONFIG_HPP_
#define METRIC_CONFIG_HPP_

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "common/capabilities.hpp"
#include "common/scmi/scmi_read_operation.hpp"

namespace astl {

/* @brief Interface for metric configuration.
 *
 * This class defines the interface that all metric configuration types must implement.
 */
class MetricConfig {
 public:
  /**
   * @brief allow destroying metric config instances by base class pointer
   */
  virtual ~MetricConfig() = default;
  /**
   * @brief Default constructor is deleted to ensure that MetricConfig cannot be instantiated directly.
   */
  MetricConfig() = delete;
  /**
   * @brief Construct a MetricConfig with the given parameters.
   */

  explicit MetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                        astl_value_type_t value_type, astl_metric_type_t metric_type, CollectorType collector_type,
                        ScmiTargetToDataEventIdMap data_event_ids)
      : _metric_name(name),
        _description(description),
        _units(units),
        _value_type(value_type),
        _metric_type(metric_type),
        _collector_type(collector_type),
        _data_event_ids(std::move(data_event_ids)) {}

  MetricConfig(const MetricConfig &)            = default;
  MetricConfig &operator=(const MetricConfig &) = default;
  MetricConfig(MetricConfig &&)                 = default;
  MetricConfig &operator=(MetricConfig &&)      = default;

  /**
   * @brief Return the name of the metric.
   *
   * @return std::string The metric name.
   */
  const std::string &Name() const { return _metric_name; }
  /**
   * @brief Return the description of the metric.
   *
   * @return std::string The metric description.
   */
  const std::string &Description() const { return _description; }
  /**
   * @brief Return the units of the metric.
   *
   * @return astl_units_t The metric units.
   */
  astl_units_t Units() const { return _units; }
  /**
   * @brief Return the value type of the metric.
   *
   * @return astl_value_type_t The metric value type.
   */
  astl_value_type_t ValueType() const { return _value_type; }
  /**
   * @brief Return the metric type.
   *
   * @return astl_metric_type_t The metric type.
   */
  astl_metric_type_t MetricType() const { return _metric_type; }
  /**
   * @brief Return the collector type of the metric.
   */
  CollectorType GetCollectorType() const { return _collector_type; }
  /**
   * @brief Set the collector type of the metric.
   *
   * @param collector_type The collector type to set.
   */
  void SetCollectorType(CollectorType collector_type) { _collector_type = collector_type; }
  /**
   * @brief Return a map of target name to Data Event ID of the metric.
   *
   * @return map of target name to data event id
   */
  virtual auto DataEventIds() const -> const ScmiTargetToDataEventIdMap & { return _data_event_ids; }

 private:
  std::string       _metric_name;  // Metric name as specified in the configuration file
  std::string       _description;  // Human-readable description of the metric
  astl_units_t      _units;        // Measurement units for the metric (e.g., seconds, bytes)
  astl_value_type_t _value_type;   // Data type of the metric value (e.g., raw, processed)
  astl_metric_type_t
                _metric_type;  // Semantic type of the metric defined by ASTL design doc(e.g., value, delta, residency)
  CollectorType _collector_type;  // Collector type to support this metric
  ScmiTargetToDataEventIdMap _data_event_ids;
};

/**
 * @brief Metric configuration for residency state counters.
 *
 * Residency metrics often consist of multiple state-specific counters (e.g., C0, C1, ...).
 * This class augments MetricConfig by allowing a mapping from target -> state name -> data event id.
 */
class ResidencyMetricConfig final : public MetricConfig {
 public:
  // Reuse the underlying data event id type from ScmiTargetToDataEventIdMap to ensure consistency.
  using DataEventId = ScmiDataEventId;  // For residency, each state has a single data event ID

  // Structure to hold both data event ID and tick frequency for a state
  struct StateInfo {
    std::string state_name;  // State name (e.g., "C1", "C6") - same as register name in JSON
    DataEventId data_event_id;
    double      tick_frequency;
  };

  using StateToInfoMap             = std::unordered_map<std::string, StateInfo>;       // state name -> state info
  using ScmiTargetToStateToInfoMap = std::unordered_map<std::string, StateToInfoMap>;  // target -> (state -> info)

  /**
   * @brief Construct a ResidencyMetricConfig with state-specific data event IDs and tick frequencies.
   *
   * Note: The base-class _data_event_ids_ map is not used for residency (multiple per target),
   * so we intentionally pass an empty map to the MetricConfig constructor.
   *
   * @param name            Metric name.
   * @param description     Human-readable description.
   * @param units           Measurement units (e.g., time, percentage).
   * @param value_type      Value representation.
   * @param metric_type     Expected to be the ASTL "residency" metric type.
   * @param collector_type  Collector type responsible for gathering residency counters.
   * @param state_info      Mapping from target -> (state name -> {data_event_id, tick_frequency}).
   * @param inferred_state  Optional state name to be inferred from the metric (e.g., "C0").
   */
  explicit ResidencyMetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                                 astl_value_type_t value_type, astl_metric_type_t metric_type,
                                 CollectorType collector_type, ScmiTargetToStateToInfoMap state_info,
                                 std::optional<std::string> inferred_state = std::nullopt)
      : MetricConfig(name, description, units, value_type, metric_type, collector_type, {}),
        _state_info(std::move(state_info)),
        _inferred_state(std::move(inferred_state)) {}

  ResidencyMetricConfig(const ResidencyMetricConfig &)            = default;
  ResidencyMetricConfig &operator=(const ResidencyMetricConfig &) = default;
  ResidencyMetricConfig(ResidencyMetricConfig &&)                 = default;
  ResidencyMetricConfig &operator=(ResidencyMetricConfig &&)      = default;
  ~ResidencyMetricConfig() override                               = default;

  /**
   * @brief Return the full mapping of residency state counters with tick frequencies.
   *
   * @return Map: target -> (state name -> {data_event_id, tick_frequency})
   */
  const ScmiTargetToStateToInfoMap &StateInfo() const { return _state_info; }

  /**
   * @brief Get the state-to-info mapping for a specific target. Throws std::out_of_range if not found.
   */
  const StateToInfoMap &StatesForTarget(const std::string &target) const { return _state_info.at(target); }

  /**
   * @brief Override DataEventIds to return flattened state event IDs for target validation.
   *
   * For residency metrics, this flattens the state-specific event IDs into a simple
   * target -> event_id map for compatibility with existing validation logic.
   * Note: Only returns the first state's event ID per target for validation purposes.
   *
   * @return Map of target name to first state's data event ID
   */
  auto DataEventIds() const -> const ScmiTargetToDataEventIdMap & override {
    // Create a static cache to return a reference
    static ScmiTargetToDataEventIdMap flattened_event_ids;
    flattened_event_ids.clear();

    for (const auto &[target_name, state_to_info] : _state_info) {
      if (!state_to_info.empty()) {
        // Collect all data event IDs for this target
        std::vector<ScmiDataEventId> target_event_ids;
        for (const auto &[state_name, state_info] : state_to_info) {
          target_event_ids.push_back(state_info.data_event_id);
        }
        flattened_event_ids[target_name] = std::move(target_event_ids);
      }
    }

    return flattened_event_ids;
  }

  /**
   * @brief Get the inferred state name if specified.
   *
   * @return Optional inferred state name
   */
  const std::optional<std::string> &InferredState() const { return _inferred_state; }

 private:
  ScmiTargetToStateToInfoMap _state_info;      // Mapping of target -> (state name -> {data_event_id, tick_frequency})
  std::optional<std::string> _inferred_state;  // Optional inferred state name
};

}  // namespace astl

#endif  // METRIC_CONFIG_HPP_
