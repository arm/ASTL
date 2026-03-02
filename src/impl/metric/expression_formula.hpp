// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef EXPRESSION_FORMULA_HPP_
#define EXPRESSION_FORMULA_HPP_

#include <cmath>
#include <expected>
#include <memory>
#include <string>
#include <type_traits>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"
#include "metric/formula.hpp"
#include "tinyexpr.h"

namespace astl {

/**
 * @brief Formula that evaluates mathematical expressions using tinyexpr++ (uint64_t mode).
 *
 * This formula parses and evaluates string expressions like "(value >> 8) & 0xFF" or "value * 2".
 * The expression must use 'value' as the variable name for the input value.
 * - @todo ASTL-287: Support Formula processing involving multiple counters.
 *
 * **UINT64_T MODE:**
 * TinyExpr++ is compiled with TE_UINT64 and TE_BITWISE_OPERATORS, providing exact uint64_t arithmetic
 * without floating-point conversion. All operations preserve full 64-bit precision.
 *
 * Supported operations:
 * - Arithmetic: +, -, *, /, %
 * - Bitwise: & (AND), | (OR), ^ (XOR), ~ (NOT)
 * - Shift: >> (right shift), << (left shift)
 * - Logical: && (and), || (or)
 * - Comparison: ==, !=, <, <=, >, >=
 * - Parentheses for grouping
 *
 * **NOTE ON FLOATING-POINT:**
 * Floating-point literals (e.g., 0.001, 0.5) will be truncated to integers, causing precision loss.
 * This happens because TinyExpr++ operates in uint64_t mode and converts all values to integers.
 * Use integer operations instead:
 * - ❌ "value * 0.001" → truncates 0.001 to 0, resulting in "value * 0" = 0
 * - ✅ "value / 1000" → correct integer division
 * - ❌ "value * 0.5" → truncates 0.5 to 0, resulting in "value * 0" = 0
 * - ✅ "value / 2" → correct integer division (equivalent to multiplying by 0.5)
 *
 * Example expressions:
 * - "value * 2"                                  // Integer multiply
 * - "value / 1000"                               // Integer divide (instead of * 0.001)
 * - "(value >> 8) & 0xFF"                        // Extract byte at position 8-15
 * - "((value >> 4) & 0xF) | ((value & 0xF) << 4)" // Swap nibbles
 * - "(value & 0xFF00) | 0x42"                    // Mask and set bits
 */
class ExpressionFormula {
 public:
  /**
   * @brief Construct an ExpressionFormula from a string expression.
   *
   * @param expression The mathematical expression to evaluate (must use 'value' as variable)
   * @return std::expected<ExpressionFormula, astl_status_code> The formula or error status
   */
  static auto Create(std::string expression) -> std::expected<ExpressionFormula, astl_status_code>;

  // Movable but not copyable (due to te_parser ownership)
  ExpressionFormula(ExpressionFormula&& other) noexcept;
  auto operator=(ExpressionFormula&& other) noexcept -> ExpressionFormula&;

  ExpressionFormula(const ExpressionFormula&)                    = delete;
  auto operator=(const ExpressionFormula&) -> ExpressionFormula& = delete;

  ~ExpressionFormula();

  [[nodiscard]] auto Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code>;

  [[nodiscard]] auto Description() const -> std::string_view { return _expression; }

  [[nodiscard]] auto GetExpression() const -> std::string_view { return _expression; }

 private:
  ExpressionFormula(std::string expression, std::unique_ptr<te_parser> parser, std::unique_ptr<te_type> input_value);

  std::string                _expression;
  std::unique_ptr<te_parser> _parser;
  std::unique_ptr<te_type>   _input_value;  // Pointer to variable that tinyexpr++ binds to
};

static_assert(Formula<ExpressionFormula>, "ExpressionFormula does not satisfy Formula concept");

}  // namespace astl

#endif  // EXPRESSION_FORMULA_HPP_
