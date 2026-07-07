// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

#include "astl_logger.hpp"
#include "metric/procfs_composite_metricconfig.hpp"
#include "tinyexpr.h"

namespace astl {

struct ProcfsCompositeMetric::ExpressionFormulaContext {
  std::string                              expression;
  std::unordered_map<std::string, te_type> variables;
  std::unique_ptr<te_parser>               parser;
};

namespace {

auto StartsWith(std::string_view value, std::string_view prefix) -> bool { return value.rfind(prefix, 0) == 0; }

auto MissingInputError(std::string_view variable_name, std::string_view input_kind, const MetricConfig& configuration)
    -> std::expected<double, astl_status_code> {
  ASTL_LOG_ERROR("ProcfsCompositeMetric: missing {} input '{}' for metric {}", input_kind, variable_name,
                 configuration.Name());
  return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
}

auto ResolvePreviousVariableValue(std::string_view                                  variable_name,
                                  const std::unordered_map<std::string, AstlValue>& previous_inputs,
                                  const MetricConfig& configuration) -> std::expected<double, astl_status_code> {
  const auto previous_it = previous_inputs.find(std::string{variable_name.substr(5)});
  if (previous_it == previous_inputs.end()) {
    return MissingInputError(variable_name, "previous", configuration);
  }
  return previous_it->second.ToDouble();
}

auto ResolveDeltaVariableValue(std::string_view                                  variable_name,
                               const std::unordered_map<std::string, AstlValue>& current_inputs,
                               const std::unordered_map<std::string, AstlValue>& previous_inputs,
                               const MetricConfig& configuration) -> std::expected<double, astl_status_code> {
  const std::string base_name{variable_name.substr(6)};
  const auto        current_it  = current_inputs.find(base_name);
  const auto        previous_it = previous_inputs.find(base_name);
  if (current_it == current_inputs.end() || previous_it == previous_inputs.end()) {
    return MissingInputError(variable_name, "delta", configuration);
  }

  auto current_value = current_it->second.ToDouble();
  if (!current_value.has_value()) {
    return std::unexpected(current_value.error());
  }
  auto previous_value = previous_it->second.ToDouble();
  if (!previous_value.has_value()) {
    return std::unexpected(previous_value.error());
  }
  return std::max(*current_value - *previous_value, 0.0);
}

auto ResolveCurrentVariableValue(std::string_view                                  variable_name,
                                 const std::unordered_map<std::string, AstlValue>& current_inputs,
                                 const MetricConfig& configuration) -> std::expected<double, astl_status_code> {
  const auto current_it = current_inputs.find(std::string{variable_name});
  if (current_it == current_inputs.end()) {
    return MissingInputError(variable_name, "current", configuration);
  }
  return current_it->second.ToDouble();
}

template <typename UnsignedT>
auto ConvertUnsignedOutputValue(double value, const MetricConfig& configuration, std::string_view type_name)
    -> std::expected<AstlValue, astl_status_code> {
  if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(std::numeric_limits<UnsignedT>::max())) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: invalid {} output value {} for metric {}", type_name, value,
                   configuration.Name());
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }
  return AstlValue{static_cast<UnsignedT>(std::llround(value))};
}

}  // namespace

ProcfsCompositeMetric::ProcfsCompositeMetric(const ProcfsCompositeMetricConfig* configuration, const ITarget* target,
                                             IProcessedSampleSink* processed_sample_sink)
    : RawMetric(configuration, target, processed_sample_sink), _procfs_configuration(configuration) {
  const auto status = InitializeExpressionFormulaParser(_procfs_configuration->FormulaText());
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: failed to initialize expression formula parser for metric {}",
                   _configuration->Name());
  }
}

ProcfsCompositeMetric::~ProcfsCompositeMetric() = default;

auto ProcfsCompositeMetric::GetOperations() -> std::expected<OperationSequence, astl_status_code> {
  _operation_id_to_input_name.clear();

  OperationSequence operations;
  operations.reserve(_procfs_configuration->Inputs().size());
  try {
    for (const auto& input : _procfs_configuration->Inputs()) {
      auto operation = std::make_unique<ProcfsReadOperation>(input.field_descriptor);
      _operation_id_to_input_name.emplace(operation->GetId(), input.name);
      operations.push_back(std::move(operation));
    }
  } catch (const OperationIdExhausted& ex) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric::GetOperations: {}", ex.what());
    _operation_id_to_input_name.clear();
    return std::unexpected{OperationIdExhausted::Status()};
  }
  return operations;
}

auto ProcfsCompositeMetric::ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code {
  astl_status_code status   = ASTL_STATUS_SUCCESS;
  const auto       input_it = _operation_id_to_input_name.find(raw_sample.operation_id);
  if (input_it == _operation_id_to_input_name.end()) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: unknown operation id {} for metric {}", raw_sample.operation_id,
                   _configuration->Name());
    status = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  } else if (!raw_sample.value.IsArithmetic()) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: non-arithmetic sample received for metric {}", _configuration->Name());
    status = ASTL_STATUS_INVALID_VALUE_TYPE;
  }
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  LogNormalizedSample(raw_sample);

  if (!_pending_batch.timestamp.has_value()) {
    _pending_batch.timestamp = raw_sample.timestamp;
  } else if (raw_sample.timestamp != *_pending_batch.timestamp) {
    if (!_pending_batch.emitted || _pending_batch.values.size() != _procfs_configuration->Inputs().size()) {
      ASTL_LOG_ERROR("ProcfsCompositeMetric: incomplete composite input set for metric {}", _configuration->Name());
      status = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
    } else {
      _pending_batch           = PendingBatch{};
      _pending_batch.timestamp = raw_sample.timestamp;
    }
  } else if (_pending_batch.emitted) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: duplicate composite batch timestamp for metric {}", _configuration->Name());
    status = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  const auto insertion_result = _pending_batch.values.emplace(input_it->second, raw_sample.value);
  if (!insertion_result.second) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: duplicate composite input '{}' for metric {}", input_it->second,
                   _configuration->Name());
    status = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  if (status == ASTL_STATUS_SUCCESS && _pending_batch.values.size() == _procfs_configuration->Inputs().size()) {
    status = EmitPendingBatch();
    if (status == ASTL_STATUS_SUCCESS) {
      _pending_batch.emitted = true;
    }
  }

  return status;
}

auto ProcfsCompositeMetric::Reset() -> void {
  _previous_inputs.clear();
  _pending_batch = PendingBatch{};
}

auto ProcfsCompositeMetric::Summarize() -> astl_status_code { return ASTL_STATUS_SUCCESS; }

auto ProcfsCompositeMetric::EmitPendingBatch() -> astl_status_code {
  if (!_pending_batch.timestamp.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  if (_procfs_configuration->RequiresPrevious() && _previous_inputs.empty()) {
    _previous_inputs = _pending_batch.values;
    return ASTL_STATUS_SUCCESS;
  }

  auto evaluated_or_error = EvaluateExpressionFormula(_pending_batch.values, _previous_inputs);
  if (!evaluated_or_error.has_value()) {
    return evaluated_or_error.error();
  }

  auto value_or_error = ConvertOutputValue(*evaluated_or_error);
  if (!value_or_error.has_value()) {
    return value_or_error.error();
  }

  _previous_inputs = _pending_batch.values;
  return SinkProcessedSample(ProcessedSampledData{*value_or_error, *_pending_batch.timestamp});
}

auto ProcfsCompositeMetric::InitializeExpressionFormulaParser(const std::string& expression_formula)
    -> astl_status_code {
  if (expression_formula.empty()) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: empty expression formula for metric {}", _configuration->Name());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  auto context        = std::make_unique<ExpressionFormulaContext>();
  context->expression = expression_formula;
  context->parser     = std::make_unique<te_parser>();

  std::set<te_variable> bound_variables;
  for (const auto& input : _procfs_configuration->Inputs()) {
    context->variables.emplace(input.name, static_cast<te_type>(0));
    if (_procfs_configuration->RequiresPrevious()) {
      context->variables.emplace("prev_" + input.name, static_cast<te_type>(0));
      context->variables.emplace("delta_" + input.name, static_cast<te_type>(0));
    }
  }

  for (auto& [name, value] : context->variables) {
    bound_variables.insert(te_variable{name, static_cast<const te_type*>(&value), TE_DEFAULT, nullptr});
  }
  context->parser->set_variables_and_functions(std::move(bound_variables));

  if (!context->parser->compile(context->expression)) {
    const auto error_pos = context->parser->get_last_error_position();
    if (error_pos >= 0) {
      ASTL_LOG_ERROR("ProcfsCompositeMetric: failed to parse expression formula '{}' at position {} for metric {}",
                     context->expression, error_pos, _configuration->Name());
    } else {
      ASTL_LOG_ERROR("ProcfsCompositeMetric: failed to parse expression formula '{}' for metric {}",
                     context->expression, _configuration->Name());
    }
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  _expression_formula_context = std::move(context);
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCompositeMetric::EvaluateExpressionFormula(const std::unordered_map<std::string, AstlValue>& current_inputs,
                                                      const std::unordered_map<std::string, AstlValue>& previous_inputs)
    const -> std::expected<double, astl_status_code> {
  if (!_expression_formula_context || !_expression_formula_context->parser) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: expression formula parser is not initialized for metric {}",
                   _configuration->Name());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  for (auto& [name, value] : _expression_formula_context->variables) {
    auto resolved = ResolveVariable(name, current_inputs, previous_inputs);
    if (!resolved.has_value()) {
      return std::unexpected(resolved.error());
    }
    value = static_cast<te_type>(*resolved);
  }

  const auto result = _expression_formula_context->parser->evaluate();
  if (result == te_parser::te_nan) {
    ASTL_LOG_ERROR("ProcfsCompositeMetric: expression formula '{}' evaluated to NaN for metric {}",
                   _expression_formula_context->expression, _configuration->Name());
    return std::unexpected(ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE);
  }
  return static_cast<double>(result);
}

auto ProcfsCompositeMetric::ResolveVariable(std::string_view                                  variable_name,
                                            const std::unordered_map<std::string, AstlValue>& current_inputs,
                                            const std::unordered_map<std::string, AstlValue>& previous_inputs) const
    -> std::expected<double, astl_status_code> {
  if (StartsWith(variable_name, "prev_")) {
    return ResolvePreviousVariableValue(variable_name, previous_inputs, *_configuration);
  }
  if (StartsWith(variable_name, "delta_")) {
    return ResolveDeltaVariableValue(variable_name, current_inputs, previous_inputs, *_configuration);
  }
  return ResolveCurrentVariableValue(variable_name, current_inputs, *_configuration);
}

auto ProcfsCompositeMetric::ConvertOutputValue(double value) const -> std::expected<AstlValue, astl_status_code> {
  std::expected<AstlValue, astl_status_code> converted_value = std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  switch (_configuration->ValueType()) {
    case ASTL_VALUE_FLOAT64:
      converted_value = AstlValue{value};
      break;
    case ASTL_VALUE_FLOAT32:
      converted_value = AstlValue{static_cast<float>(value)};
      break;
    case ASTL_VALUE_UINT64:
      converted_value = ConvertUnsignedOutputValue<uint64_t>(value, *_configuration, "uint64");
      break;
    case ASTL_VALUE_UINT32:
      converted_value = ConvertUnsignedOutputValue<uint32_t>(value, *_configuration, "uint32");
      break;
    default:
      ASTL_LOG_ERROR("ProcfsCompositeMetric: unsupported output value type {} for metric {}",
                     _configuration->ValueType(), _configuration->Name());
      break;
  }
  return converted_value;
}

}  // namespace astl
