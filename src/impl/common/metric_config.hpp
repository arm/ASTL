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

/**
 * @file metric_config.hpp
 * @brief Metric configuration interfaces and concrete config specializations.
 */
#ifndef METRIC_CONFIG_HPP_
#define METRIC_CONFIG_HPP_

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "common/capabilities.hpp"
#include "operation/operation_builder.hpp"

namespace astl {

/**
 * @brief Abstract interface describing the immutable configuration of a metric.
 *
 * Provides the common descriptive & structural fields (name, description, units, value type,
 * semantic metric type, collector type and underlying per-target data event IDs) used during
 * registration & validation before collection begins.
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
   * @param name           Metric name.
   * @param description    Human-readable description of the metric.
   * @param units          Measurement units for the metric (e.g., Watts, Joules).
   * @param value_type     Data type of the metric value (e.g., uint32, float64).
   * @param metric_type    Semantic type of the metric defined by ASTL design doc (e.g. value, delta, residency)
   * @param category       High-level domain category (e.g., Power, Temperature).
   * @param collector_type Collector type responsible for gathering this metric (e.g., SCMI, Libsensors).
   * @param operation_builder The operation builder associated with this metric's collector type,
   *                          including collector-specific parameters like data event id or libsensors chip
   *
   * REFACTOR - Eliminate this function.
   * We should just have one parameterized constructor with every parameter available.
   * If we don't want to pass each value every time, we can use default parameters.
   */
  explicit MetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                        astl_value_type_t value_type, astl_category_t category, astl_metric_type_t metric_type,
                        CollectorType collector_type, AnyOperationBuilder operation_builder)
      : _metric_name(name),
        _description(description),
        _units(units),
        _value_type(value_type),
        _metric_type(metric_type),
        _category(category),
        _collector_type(collector_type),
        _operation_builder(std::move(operation_builder)) {}

  /**
   * @brief Construct a MetricConfig with the given parameters.
   * @param name           Metric name.
   * @param description    Human-readable description of the metric.
   * @param units          Measurement units for the metric (e.g., Watts, Joules).
   * @param value_type     Data type of the metric value (e.g., uint32, float64).
   * @param metric_type    Semantic type of the metric defined by ASTL design doc (e.g. value, delta, residency)
   * @param category       High-level domain category (e.g., Power, Temperature).
   * @param metric_groups  vector of strings representin the names of metric gropus this belongs to
   * @param collector_type Collector type responsible for gathering this metric (e.g., SCMI, Libsensors).
   * @param operation_builder The operation builder associated with this metric's collector type,
   *                          including collector-specific parameters like data event id or libsensors chip
   */
  explicit MetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                        astl_value_type_t value_type, astl_category_t category, astl_metric_type_t metric_type,
                        std::vector<std::string> metric_groups, CollectorType collector_type,
                        AnyOperationBuilder operation_builder)
      : _metric_name(name),
        _description(description),
        _units(units),
        _value_type(value_type),
        _metric_type(metric_type),
        _category(category),
        _metric_groups(std::move(metric_groups)),
        _collector_type(collector_type),
        _operation_builder(std::move(operation_builder)) {}

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
   * @brief Return the high-level metric category (e.g. Power, Temperature).
   *
   * @return astl_category_t The metric category.
   */
  astl_category_t Category() const { return _category; }
  /**
   * @brief Return the collector type of the metric.
   */
  CollectorType GetCollectorType() const { return _collector_type; }

  /**
   * @brief Return the groups this metric belongs to.
   */
  auto MetricGroups() const -> std::vector<std::string> const & { return _metric_groups; }

  /**
   * @brief return the operation builder, for use with operation_builder.hpp's `BuildOperations`
   */
  auto GetOperationBuilder() const -> AnyOperationBuilder const & { return _operation_builder; }

 private:
  std::string       _metric_name;  // Metric name as specified in the configuration file
  std::string       _description;  // Human-readable description of the metric
  astl_units_t      _units;        // Measurement units for the metric (e.g., seconds, bytes)
  astl_value_type_t _value_type;   // Data type of the metric value (e.g., raw, processed)
  // Semantic type of the metric defined by ASTL design doc(e.g., value, delta, residency)
  astl_metric_type_t       _metric_type;     // Semantic metric type (value, delta, residency, etc.)
  astl_category_t          _category;        // High-level domain category (power, temperature, count, etc.)
  std::vector<std::string> _metric_groups;   // Groups this metric belongs to
  CollectorType            _collector_type;  // Collector type to support this metric
  AnyOperationBuilder      _operation_builder;
};

/**
 * @brief Metric configuration for residency state counters.
 *
 * Residency metrics often consist of multiple state-specific counters (e.g., C0, C1, ...).
 * This class augments MetricConfig by allowing a mapping from target -> state name -> data event id.
 */
class ResidencyMetricConfig final : public MetricConfig {
 public:
  // Reuse the underlying data event id type from TargetToDataEventIdMap to ensure consistency.
  using DataEventId = ScmiDataEventId;  // For residency, each state has a single data event ID

  // Structure to hold both data event ID and tick frequency for a state
  struct StateInfo {
    std::string         state_name;  // State name (e.g., "C1", "C6") - same as register name in JSON
    double              tick_frequency;
    AnyOperationBuilder operation_builder;
  };

  using StateToInfoMap         = std::unordered_map<std::string, StateInfo>;       // state name -> state info
  using TargetToStateToInfoMap = std::unordered_map<std::string, StateToInfoMap>;  // target -> (state -> info)

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
   * @param state_info      Mapping from target -> (state name -> {operation_builder, tick_frequency}).
   * @param inferred_state  Optional state name to be inferred from the metric (e.g., "C0").
   */
  explicit ResidencyMetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                                 astl_value_type_t value_type, astl_metric_type_t metric_type, astl_category_t category,
                                 CollectorType collector_type, TargetToStateToInfoMap state_info,
                                 std::optional<std::string> inferred_state = std::nullopt)
      : MetricConfig(name, description, units, value_type, category, metric_type, collector_type,
                     NullOperationBuilder{}),
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
  const TargetToStateToInfoMap &GetStateInfo() const { return _state_info; }

  /**
   * @brief Get the state-to-info mapping for a specific target. Throws std::out_of_range if not found.
   */
  const StateToInfoMap &StatesForTarget(const std::string &target) const { return _state_info.at(target); }

  /**
   * @brief Get the inferred state name if specified.
   *
   * @return Optional inferred state name
   */
  const std::optional<std::string> &InferredState() const { return _inferred_state; }

 private:
  TargetToStateToInfoMap     _state_info;      // Mapping of target -> (state name -> {data_event_id, tick_frequency})
  std::optional<std::string> _inferred_state;  // Optional inferred state name
};

/**
 * @brief Metric configuration for finite set metrics.
 *
 * Finite set metrics track values that can only take on a predefined set of known values.
 * This class extends MetricConfig to include the finite set definition and validation.
 */
class FiniteSetMetricConfig final : public MetricConfig {
 public:
  // Forward declaration for AstlValue - will need to include the appropriate header
  using FiniteSet       = std::set<AstlValue>;
  using ValueToLabelMap = std::map<AstlValue, std::string>;  ///< Mapping from value -> human-readable label

  /**
   * @brief Construct a FiniteSetMetricConfig with a predefined set of valid values.
   *
   * @param name            Metric name.
   * @param description     Human-readable description.
   * @param units           Measurement units.
   * @param value_type      Value representation type.
   * @param metric_type     Expected to be a finite set metric type.
   * @param collector_type  Collector type responsible for gathering finite set data.
   * @param operation_builder  variant type that will create operations for the given target for a certain collector
   * type
   * @param finite_set      Set of valid AstlValue objects that define the finite set.
   * @param labels          Mapping from finite set values to human-readable labels.
   */
  explicit FiniteSetMetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                                 astl_value_type_t value_type, astl_metric_type_t metric_type, astl_category_t category,
                                 CollectorType collector_type, AnyOperationBuilder operation_builder,
                                 FiniteSet finite_set, ValueToLabelMap labels)
      : MetricConfig(name, description, units, value_type, category, metric_type, collector_type,
                     std::move(operation_builder)),
        _finite_set(std::move(finite_set)),
        _labels(std::move(labels)) {}

  FiniteSetMetricConfig(const FiniteSetMetricConfig &)            = default;
  FiniteSetMetricConfig &operator=(const FiniteSetMetricConfig &) = default;
  FiniteSetMetricConfig(FiniteSetMetricConfig &&)                 = default;
  FiniteSetMetricConfig &operator=(FiniteSetMetricConfig &&)      = default;
  ~FiniteSetMetricConfig() override                               = default;

  /**
   * @brief Get the finite set of valid values.
   *
   * @return The set of valid AstlValue objects.
   */
  const FiniteSet &GetFiniteSet() const { return _finite_set; }

  /**
   * @brief Check if a value is in the predefined finite set.
   *
   * @param value The AstlValue to check.
   * @return true if the value is in the finite set, false otherwise.
   */
  bool IsInFiniteSet(const AstlValue &value) const { return _finite_set.contains(value); }

  /**
   * @brief Get the size of the finite set.
   *
   * @return Number of valid values in the finite set.
   */
  size_t FiniteSetSize() const { return _finite_set.size(); }

  /**
   * @brief Get the labels mapping from values to human-readable labels.
   *
   * @return Map of finite set values to their labels.
   */
  const ValueToLabelMap &GetLabels() const { return _labels; }

  /**
   * @brief Get the label for a specific value, if it exists.
   *
   * @param value The AstlValue to get the label for.
   * @return The label string if found, empty string otherwise.
   */
  std::string GetLabelForValue(const AstlValue &value) const {
    auto it = _labels.find(value);
    return (it != _labels.end()) ? it->second : "";
  }

 private:
  FiniteSet       _finite_set;  ///< The set of valid AstlValue objects
  ValueToLabelMap _labels;      ///< Mapping from finite set values to human-readable labels
};

}  // namespace astl

#endif  // METRIC_CONFIG_HPP_
