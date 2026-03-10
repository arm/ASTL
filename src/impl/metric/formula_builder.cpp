// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/formula_builder.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <type_traits>
#include <utility>

#include "astl_logger.hpp"
#include "astl_utils.hpp"

namespace astl {

// Formula expressions are powered by tinyexpr++, supporting:
// - Arithmetic operations: +, -, *, /, %
// - Shift operators: >> (right shift), << (left shift) - native operators
// - Bitwise functions: bitand(), bitor(), bitxor(), bitnot(), bitlshift(), bitrshift()
// - Logical operators: && (and), || (or), & (and), | (or)
// - Mathematical functions: abs, sin, cos, sqrt, pow, ln, log, exp, etc.
// - Complex expressions with parentheses for proper precedence
// - Variable 'value' represents the input metric value
//
// IMPORTANT: & and | are LOGICAL operators, not bitwise! Use bitand(), bitor() for bitwise ops.
//
// Examples:
//   "value / 1000"                       - Simple scaling
//   "bitand(value >> 8, 0xFF)"           - Extract byte (bits 8-15): shift is native, bitand is function
//   "bitand(value >> 4, 0xFF) / 2"       - Multi-step transformation
//
// @todo ASTL-255: Support Q notation for fixed-point format as part of transformations.

static auto ContainsNonIntegerNumericLiteral(std::string_view expression) -> bool {
  // Reject floating/scientific numeric literals to avoid TE_UINT64 truncation surprises.
  // Allowed examples: 42, 0xFF. Rejected examples: 0.5, 1., .25, 1e3, 1E-3.
  static const std::regex fractional_literal_pattern{
      R"((^|[^A-Za-z0-9_])([0-9]*\.[0-9]+|[0-9]+\.[0-9]*)([^A-Za-z0-9_]|$))"};
  static const std::regex scientific_literal_pattern{
      R"((^|[^A-Za-z0-9_])[0-9]+(\.[0-9]*)?[eE][+-]?[0-9]+([^A-Za-z0-9_]|$))"};
  const std::string expression_text{expression};
  return std::regex_search(expression_text, fractional_literal_pattern) ||
         std::regex_search(expression_text, scientific_literal_pattern);
}

static auto BuildFormulaHelper(const std::optional<nlohmann::json>& formula_json)
    -> std::expected<AnyFormula, astl_status_code> {
  // No formula specified - use identity (pass-through)
  if (!formula_json.has_value() || formula_json->is_null()) {
    return AnyFormula{IdentityFormula{}};
  }

  // Support string expressions like "bitand(value >> 8, 0xFF)" or "value / 1000"
  if (formula_json->is_string()) {
    std::string expression = formula_json->get<std::string>();
    if (expression.empty()) {
      ASTL_LOG_WARNING("Empty formula string, treating as no formula");
      return AnyFormula{IdentityFormula{}};
    }
    if (ContainsNonIntegerNumericLiteral(expression)) {
      ASTL_LOG_ERROR("Only integer numeric literals are allowed in formulas: '{}'.", expression);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }

    ASTL_LOG_DEBUG("Parsing formula expression: '{}'", expression);
    auto result = ExpressionFormula::Create(std::move(expression));
    if (!result.has_value()) {
      ASTL_LOG_ERROR("Failed to create ExpressionFormula: {}", astlStatusString(result.error()));
      return std::unexpected(result.error());
    }
    AnyFormula formula{std::move(result.value())};
    ASTL_LOG_DEBUG("Successfully parsed formula: {}", GetFormulaDescription(formula));
    return formula;
  }

  ASTL_LOG_ERROR("Formula must be a string expression (e.g., \"value / 1000\" or \"bitand(value >> 8, 0xFF)\")");
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

static auto IsIdentityFormula(const AnyFormula& formula) -> bool {
  return std::holds_alternative<IdentityFormula>(formula);
}

/**
 * @brief Append a formula as one or more pipeline steps.
 *
 * Identity formulas are dropped, concrete formulas are appended directly, and
 * nested pipelines are flattened to keep execution iteration shallow.
 */
static auto AppendPipelineSteps(std::vector<FormulaPipeline::PipelineStep>& steps, AnyFormula formula) -> void {
  // Identity formula has no effect, so skip it to keep pipelines minimal.
  if (std::holds_alternative<IdentityFormula>(formula)) {
    return;
  }
  if (std::holds_alternative<ScalingFormula>(formula)) {
    steps.emplace_back(std::move(std::get<ScalingFormula>(formula)));
    return;
  }
  if (std::holds_alternative<ExpressionFormula>(formula)) {
    steps.emplace_back(std::move(std::get<ExpressionFormula>(formula)));
    return;
  }
  // Flatten nested pipelines so downstream Apply() only iterates one level.
  auto pipeline_steps = std::move(std::get<FormulaPipeline>(formula)).TakeSteps();
  std::transform(std::make_move_iterator(pipeline_steps.begin()), std::make_move_iterator(pipeline_steps.end()),
                 std::back_inserter(steps), [](FormulaPipeline::PipelineStep&& step) { return std::move(step); });
}

auto BuildFormula(const std::optional<nlohmann::json>& formula_json) noexcept
    -> std::expected<AnyFormula, astl_status_code> {
  try {
    return BuildFormulaHelper(formula_json);
  } catch (const std::exception& ex) {
    ASTL_LOG_ERROR("Exception while building formula: {}", ex.what());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
}

auto ComposeFormulas(AnyFormula first, AnyFormula second) -> AnyFormula {
  // Fast-path single formula cases and avoid allocating a pipeline.
  if (IsIdentityFormula(first)) {
    return second;
  }
  if (IsIdentityFormula(second)) {
    return first;
  }

  std::vector<FormulaPipeline::PipelineStep> steps;
  steps.reserve(4);
  AppendPipelineSteps(steps, std::move(first));
  AppendPipelineSteps(steps, std::move(second));

  if (steps.empty()) {
    return AnyFormula{IdentityFormula{}};
  }
  if (steps.size() == 1) {
    // Keep single-step result as the concrete formula variant for simpler debugging.
    return std::visit(
        [](auto&& formula_impl) -> AnyFormula {
          return AnyFormula{std::forward<decltype(formula_impl)>(formula_impl)};
        },
        std::move(steps.front()));
  }
  // Multi-step formulas are represented explicitly as a pipeline.
  return AnyFormula{FormulaPipeline{std::move(steps)}};
}

static auto FormatScaleAppliedToValueForApi(const std::string& lhs, uint64_t numerator, uint64_t denominator)
    -> std::string;

/**
 * @brief Apply a scaling factor to an expression for API rendering.
 *
 * Scaling is rendered from exact integer ratio terms so the emitted formula
 * stays parseable by TinyExpr clients.
 */
static auto FormatScaleAppliedToValueForApi(const std::string& lhs, uint64_t numerator, uint64_t denominator)
    -> std::string {
  if (denominator == 0) {
    return "(" + lhs + ") / 0";
  }
  if (numerator == denominator) {
    return lhs;
  }
  if (denominator == 1) {
    return lhs + " * " + std::to_string(numerator);
  }
  if (numerator == 1) {
    return lhs + " / " + std::to_string(denominator);
  }
  return "((" + lhs + ") * " + std::to_string(numerator) + ") / " + std::to_string(denominator);
}

// Replace standalone "value" variable tokens only (not substrings inside identifiers).
static auto ReplaceValueVariable(std::string_view expression, std::string_view replacement) -> std::string {
  std::string result;
  result.reserve(expression.size() + replacement.size());
  constexpr std::string_view value_token = "value";

  auto is_identifier_char = [](char character) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    return std::isalnum(unsigned_character) != 0 || character == '_';
  };

  for (size_t i = 0; i < expression.size();) {
    const bool has_value_token =
        i + value_token.size() <= expression.size() && expression.compare(i, value_token.size(), value_token) == 0;
    if (has_value_token) {
      const bool left_boundary = i == 0 || !is_identifier_char(expression[i - 1]);
      const bool right_boundary =
          i + value_token.size() == expression.size() || !is_identifier_char(expression[i + value_token.size()]);
      if (left_boundary && right_boundary) {
        result += replacement;
        i += value_token.size();
        continue;
      }
    }
    result.push_back(expression[i]);
    ++i;
  }
  return result;
}

static auto ComposePipelineStepIntoExpression(std::string current_expression, const FormulaPipeline::PipelineStep& step)
    -> std::string {
  return std::visit(
      [&current_expression](const auto& impl) -> std::string {
        using T = std::decay_t<decltype(impl)>;
        if constexpr (std::is_same_v<T, IdentityFormula>) {
          return current_expression;
        }
        if constexpr (std::is_same_v<T, ScalingFormula>) {
          // Wrap the current expression before applying scaling to preserve operator precedence.
          return FormatScaleAppliedToValueForApi(std::string{"("} + current_expression + ")", impl.GetNumerator(),
                                                 impl.GetDenominator());
        }
        return ReplaceValueVariable(std::string_view{impl.Description()}, std::string{"("} + current_expression + ")");
      },
      step);
}

static auto FormatFormulaImplForApi(const IdentityFormula& /*unused*/) -> std::string { return "value"; }

static auto FormatFormulaImplForApi(const ScalingFormula& formula_impl) -> std::string {
  return FormatScaleAppliedToValueForApi("value", formula_impl.GetNumerator(), formula_impl.GetDenominator());
}

static auto FormatFormulaImplForApi(const ExpressionFormula& formula_impl) -> std::string {
  return std::string{formula_impl.Description()};
}

/**
 * @brief Render a formula pipeline for API exposure.
 *
 * Pipelines are rendered as a single TinyExpr-safe expression by composing each
 * step into the current expression. This keeps `_formula` parseable end-to-end
 * even for multi-step pipelines.
 */
static auto FormatFormulaImplForApi(const FormulaPipeline& formula_impl) -> std::string {
  if (formula_impl.Steps().empty()) {
    return "value";
  }
  std::string result = "value";
  for (const auto& step : formula_impl.Steps()) {
    result = ComposePipelineStepIntoExpression(std::move(result), step);
  }
  return result;
}

auto FormatFormulaForApi(const AnyFormula& formula) -> std::string {
  // Keep API rendering logic centralized so counter properties and metric semantics stay in sync.
  return std::visit([](const auto& impl) { return FormatFormulaImplForApi(impl); }, formula);
}

}  // namespace astl
