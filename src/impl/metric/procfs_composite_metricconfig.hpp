// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_COMPOSITE_METRICCONFIG_HPP_
#define PROCFS_COMPOSITE_METRICCONFIG_HPP_

#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/metric_config.hpp"
#include "common/procfs_utils.hpp"
#include "metric/raw_metric.hpp"
#include "operation/procfs_read_operation.hpp"

namespace astl {

class ProcfsCompositeMetricConfig final : public MetricConfig {
 public:
  struct InputBinding {
    std::string             name;
    procfs::FieldDescriptor field_descriptor;
  };

  struct CreateParams {
    std::string               name;
    std::string               description;
    astl_units_t              units{ASTL_UNITS_NONE};
    astl_value_type_t         value_type{ASTL_VALUE_UNKNOWN};
    astl_metric_identifier_t  identifier{ASTL_METRIC_IDENTIFIER_UNKNOWN};
    astl_metric_type_t        metric_type{ASTL_METRIC_UNKNOWN};
    std::vector<InputBinding> inputs;
    bool                      requires_previous{false};
    std::string               formula_text;
    std::vector<std::string>  metric_groups;
    std::string               metric_id;
  };

  explicit ProcfsCompositeMetricConfig(CreateParams params)
      : MetricConfig(params.name, params.description, params.units, params.value_type, params.identifier,
                     params.metric_type, CollectorType::PROCFS, NullOperationBuilder{}, IdentityFormula{},
                     ASTL_VALUE_UNKNOWN, std::move(params.metric_groups), std::move(params.metric_id)),
        _inputs(std::move(params.inputs)),
        _requires_previous(params.requires_previous),
        _formula_text(std::move(params.formula_text)) {}

  auto Inputs() const -> const std::vector<InputBinding>& { return _inputs; }
  auto RequiresPrevious() const -> bool { return _requires_previous; }
  auto FormulaText() const -> const std::string& { return _formula_text; }

 private:
  std::vector<InputBinding> _inputs;
  bool                      _requires_previous{false};
  std::string               _formula_text;
};

class ProcfsCompositeMetric final : public RawMetric {
 public:
  explicit ProcfsCompositeMetric(const ProcfsCompositeMetricConfig* configuration, const ITarget* target,
                                 IProcessedSampleSink* processed_sample_sink);
  ProcfsCompositeMetric(const ProcfsCompositeMetric&)                        = delete;
  auto operator=(const ProcfsCompositeMetric&) -> ProcfsCompositeMetric&     = delete;
  ProcfsCompositeMetric(ProcfsCompositeMetric&&) noexcept                    = delete;
  auto operator=(ProcfsCompositeMetric&&) noexcept -> ProcfsCompositeMetric& = delete;
  ~ProcfsCompositeMetric() override;

  auto GetOperations() -> std::expected<OperationSequence, astl_status_code> override;
  auto ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code override;
  auto Reset() -> void override;
  auto Summarize() -> astl_status_code override;
  auto RestoreOperationInputBindings(std::span<const OperationId> operation_ids) -> astl_status_code;

 private:
  struct PendingBatch {
    std::optional<ProcessedSampleTimestamp>    timestamp;
    std::unordered_map<std::string, AstlValue> values;
    bool                                       emitted{false};
  };

  auto EmitPendingBatch() -> astl_status_code;
  auto InitializeExpressionFormulaParser(const std::string& expression_formula) -> astl_status_code;
  auto EvaluateExpressionFormula(const std::unordered_map<std::string, AstlValue>& current_inputs,
                                 const std::unordered_map<std::string, AstlValue>& previous_inputs) const
      -> std::expected<double, astl_status_code>;
  auto ResolveVariable(std::string_view variable_name, const std::unordered_map<std::string, AstlValue>& current_inputs,
                       const std::unordered_map<std::string, AstlValue>& previous_inputs) const
      -> std::expected<double, astl_status_code>;
  auto ConvertOutputValue(double value) const -> std::expected<AstlValue, astl_status_code>;

  struct ExpressionFormulaContext;

  const ProcfsCompositeMetricConfig*           _procfs_configuration;
  std::unordered_map<OperationId, std::string> _operation_id_to_input_name;
  std::unordered_map<std::string, AstlValue>   _previous_inputs;
  PendingBatch                                 _pending_batch;
  std::unique_ptr<ExpressionFormulaContext>    _expression_formula_context;
};

}  // namespace astl

#endif  // PROCFS_COMPOSITE_METRICCONFIG_HPP_
