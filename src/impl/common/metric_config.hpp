// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file metric_config.hpp
 * @brief Metric configuration interfaces and concrete config specializations.
 */
#ifndef METRIC_CONFIG_HPP_
#define METRIC_CONFIG_HPP_

#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "common/capabilities.hpp"
#include "metric/formula_builder.hpp"
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
   * @param name           User-facing metric name.
   * @param description    Human-readable description of the metric.
   * @param units          Measurement units for the metric (e.g., Watts, Joules).
   * @param value_type     Data type of the metric value (e.g., uint32, float64).
   * @param metric_type    Semantic type of the metric defined by ASTL design doc (e.g. value, delta, residency)
   * @param identifier       High-level domain identifier (e.g., Power, Temperature).
   * @param collector_type Collector type responsible for gathering this metric (e.g., SCMI, Libsensors).
   * @param operation_builder The operation builder associated with this metric's collector type,
   *                          including collector-specific parameters like data event id or libsensors chip.
   * @param formula        Formula for processing raw samples (ExpressionFormula or IdentityFormula).
   * @param input_value_type Raw collector sample type before metric-level processing.
   * @param metric_groups  Optional group names this metric belongs to.
   * @param metric_id      Optional stable internal identifier. Defaults to the metric name when omitted.
   */
  template <AnyOperationBuilderCompatible OperationBuilderType>
  explicit MetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                        astl_value_type_t value_type, astl_metric_identifier_t identifier,
                        astl_metric_type_t metric_type, CollectorType collector_type,
                        OperationBuilderType &&operation_builder, AnyFormula formula = IdentityFormula{},
                        astl_value_type_t        input_value_type = ASTL_VALUE_UNKNOWN,
                        std::vector<std::string> metric_groups = {}, std::string metric_id = {})
      : _metric_id(metric_id.empty() ? name : std::move(metric_id)),
        _metric_name(name),
        _description(description),
        _units(units),
        _value_type(value_type),
        // Default input type to the output type unless caller explicitly separates them.
        _input_value_type(input_value_type == ASTL_VALUE_UNKNOWN ? value_type : input_value_type),
        _metric_type(metric_type),
        _identifier(identifier),
        _metric_groups(std::move(metric_groups)),
        _collector_type(collector_type),
        _operation_builder(std::forward<OperationBuilderType>(operation_builder)),
        _formula(std::move(formula)) {}

  // Delete copy operations since ExpressionFormula is move-only
  MetricConfig(const MetricConfig &)            = delete;
  MetricConfig &operator=(const MetricConfig &) = delete;

  // Allow move operations
  MetricConfig(MetricConfig &&)            = default;
  MetricConfig &operator=(MetricConfig &&) = default;

  /**
   * @brief Return the user-facing name of the metric.
   *
   * @return std::string The metric name.
   */
  const std::string &Name() const { return _metric_name; }
  /**
   * @brief Return the stable internal identifier of the metric.
   *
   * This may differ from Name() when the metric is presented with a friendly
   * label but still needs a unique identifier for routing or serialization.
   */
  const std::string &Id() const { return _metric_id; }
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
   * @brief Return the expected collector sample type before metric-level processing.
   */
  astl_value_type_t InputValueType() const { return _input_value_type; }
  /**
   * @brief Return the metric type.
   *
   * @return astl_metric_type_t The metric type.
   */
  astl_metric_type_t MetricType() const { return _metric_type; }
  /**
   * @brief Return the high-level metric identifier (e.g. Power, Temperature).
   *
   * @return astl_metric_identifier_t The metric identifier.
   */
  astl_metric_identifier_t Identifier() const { return _identifier; }
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

  /**
   * @brief Return the formula for processing raw samples.
   */
  auto GetFormula() const -> const AnyFormula & { return _formula; }

  /**
   * @brief Override the user-facing name without changing the internal identifier.
   */
  auto SetName(std::string name) -> void { _metric_name = std::move(name); }

 private:
  std::string       _metric_id;         // Stable internal metric identifier used for routing/serialization
  std::string       _metric_name;       // User-facing metric label exposed via API properties
  std::string       _description;       // Human-readable description of the metric
  astl_units_t      _units;             // Measurement units for the metric (e.g., seconds, bytes)
  astl_value_type_t _value_type;        // Data type of the metric value (e.g., raw, processed)
  astl_value_type_t _input_value_type;  // Collector-provided raw sample type before transformations
  // Semantic type of the metric defined by ASTL design doc(e.g., value, delta, residency)
  astl_metric_type_t       _metric_type;     // Semantic metric type (value, delta, residency, etc.)
  astl_metric_identifier_t _identifier;      // High-level domain identifier (power, temperature, count, etc.)
  std::vector<std::string> _metric_groups;   // Groups this metric belongs to
  CollectorType            _collector_type;  // Collector type to support this metric
  AnyOperationBuilder      _operation_builder;
  AnyFormula               _formula;  // Formula for processing raw samples (ExpressionFormula, IdentityFormula)
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
    std::string         state_name;         // State name (e.g., "C1", "C6") - same as register name in JSON
    std::string         state_description;  // Description of the state
    double              tick_frequency;
    AnyOperationBuilder operation_builder;
  };

  using StateToInfoMap = std::unordered_map<std::string, StateInfo>;  // state name -> state info

  // Structure to hold name and description for the inferred state
  struct InferredStateInfo {
    std::string name;         // State name (e.g., "C0", "Active")
    std::string description;  // Description of the inferred state
  };

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
   * @param state_info      Mapping from state name -> {operation_builder, tick_frequency}.
   * @param inferred_state  Optional inferred state info (name and description).
   * @param formula         Formula for processing raw samples (ExpressionFormula or IdentityFormula).
   */
  explicit ResidencyMetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                                 astl_value_type_t value_type, astl_metric_type_t metric_type,
                                 astl_metric_identifier_t identifier, CollectorType collector_type,
                                 StateToInfoMap                   state_info,
                                 std::optional<InferredStateInfo> inferred_state = std::nullopt,
                                 AnyFormula                       formula        = IdentityFormula{},
                                 astl_value_type_t input_value_type = ASTL_VALUE_UNKNOWN, std::string metric_id = {})
      : MetricConfig(name, description, units, value_type, identifier, metric_type, collector_type,
                     NullOperationBuilder{}, std::move(formula), input_value_type, {}, std::move(metric_id)),
        _state_info(std::move(state_info)),
        _inferred_state(std::move(inferred_state)) {}

  ResidencyMetricConfig(const ResidencyMetricConfig &)            = delete;
  ResidencyMetricConfig &operator=(const ResidencyMetricConfig &) = delete;
  ResidencyMetricConfig(ResidencyMetricConfig &&)                 = default;
  ResidencyMetricConfig &operator=(ResidencyMetricConfig &&)      = default;
  ~ResidencyMetricConfig() override                               = default;

  /**
   * @brief Return the full mapping of residency state counters with tick frequencies.
   *
   * @return Map: state name -> {data_event_id, tick_frequency}
   */
  auto GetStateInfo() const -> StateToInfoMap const & { return _state_info; }

  /**
   * @brief Get the inferred state info if specified.
   *
   * @return Optional inferred state info (name and description)
   */
  const std::optional<InferredStateInfo> &InferredState() const { return _inferred_state; }

 private:
  StateToInfoMap                   _state_info;  // Mapping of target -> (state name -> {data_event_id, tick_frequency})
  std::optional<InferredStateInfo> _inferred_state;  // Optional inferred state name and description
};

/**
 * @brief Metric configuration for finite set metrics.
 *
 * Finite set metrics track values that can only take on a predefined set of known values.
 * This class extends MetricConfig to include the finite set definition and validation.
 */
class FiniteSetMetricConfig final : public MetricConfig {
 public:
  // Structure to hold value, label, and description for a finite set state
  struct StateInfo {
    std::string state_name;         // Human-readable label
    std::string state_description;  // Description of the state
  };

  // Forward declaration for AstlValue - will need to include the appropriate header
  using FiniteSet      = std::set<AstlValue>;
  using ValueToInfoMap = std::map<AstlValue, StateInfo>;  ///< Mapping from value -> state info (label + description)

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
   * @param state_info      Mapping from finite set values to state info (label + description).
   * @param formula         Formula for processing raw samples (ExpressionFormula or IdentityFormula).
   */
  template <AnyOperationBuilderCompatible OperationBuilderType>
  explicit FiniteSetMetricConfig(const std::string &name, const std::string &description, astl_units_t units,
                                 astl_value_type_t value_type, astl_metric_type_t metric_type,
                                 astl_metric_identifier_t identifier, CollectorType collector_type,
                                 OperationBuilderType &&operation_builder, FiniteSet finite_set,
                                 ValueToInfoMap state_info, AnyFormula formula = IdentityFormula{},
                                 astl_value_type_t        input_value_type = ASTL_VALUE_UNKNOWN,
                                 std::vector<std::string> metric_groups = {}, std::string metric_id = {})
      : MetricConfig(name, description, units, value_type, identifier, metric_type, collector_type,
                     std::forward<OperationBuilderType>(operation_builder), std::move(formula), input_value_type,
                     std::move(metric_groups), std::move(metric_id)),
        _finite_set(std::move(finite_set)),
        _state_info(std::move(state_info)) {}

  FiniteSetMetricConfig(const FiniteSetMetricConfig &)            = delete;
  FiniteSetMetricConfig &operator=(const FiniteSetMetricConfig &) = delete;
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
   * @brief Get the state info mapping from values to state information.
   *
   * @return Map of finite set values to their state info (label + description).
   */
  const ValueToInfoMap &GetStateInfo() const { return _state_info; }

  /**
   * @brief Get the state info for a specific value, if it exists.
   *
   * @param value The AstlValue to get the state info for.
   * @return std::expected containing a pointer to StateInfo if found, or
   * std::unexpected(ASTL_STATUS_BAD_CONFIGURATION) if the value is not present.
   */
  [[nodiscard]] std::expected<const StateInfo *, astl_status_code> GetStateInfoForValue(const AstlValue &value) const {
    auto iter = _state_info.find(value);
    if (iter == _state_info.end()) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return &iter->second;
  }

 private:
  FiniteSet      _finite_set;  ///< The set of valid AstlValue objects
  ValueToInfoMap _state_info;  ///< Mapping from value -> state info (label + description)
};

using MetricConfigOnTargets =
    std::unordered_map<std::unique_ptr<MetricConfig>,
                       std::vector<const ITarget *>>;  //< MetricConfig mapped to applicable targets

}  // namespace astl

#endif  // METRIC_CONFIG_HPP_
