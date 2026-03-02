// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/formula_builder.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>

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
//   "value * 0.001"                      - Simple scaling
//   "bitand(value >> 8, 0xFF)"           - Extract byte (bits 8-15): shift is native, bitand is function
//   "bitand(value >> 4, 0xFF) * 0.5"     - Multi-step transformation
//
// @todo ASTL-255: Support Q notation for fixed-point format as part of transformations.

static auto BuildFormulaHelper(const std::optional<nlohmann::json>& formula_json)
    -> std::expected<AnyFormula, astl_status_code> {
  // No formula specified - use identity (pass-through)
  if (!formula_json.has_value() || formula_json->is_null()) {
    return AnyFormula{IdentityFormula{}};
  }

  // Support string expressions like "bitand(value >> 8, 0xFF)" or "value * 0.001"
  if (formula_json->is_string()) {
    std::string expression = formula_json->get<std::string>();
    if (expression.empty()) {
      ASTL_LOG_WARNING("Empty formula string, treating as no formula");
      return AnyFormula{IdentityFormula{}};
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

  ASTL_LOG_ERROR("Formula must be a string expression (e.g., \"value * 0.001\" or \"bitand(value >> 8, 0xFF)\")");
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
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

}  // namespace astl
