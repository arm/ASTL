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
 * @brief Formula that evaluates mathematical expressions using tinyexpr++.
 *
 * This formula parses and evaluates string expressions like "bitand(value >> 8, 0xFF)" or "(value - 50) * 0.5".
 * The expression must use 'value' as the variable name for the input value.
 * - @todo ASTL-287: Support Formula processing involving multiple counters.
 *
 * Supports standard mathematical operations:
 * - Arithmetic: +, -, *, /, %
 * - Shift operators: >> (right shift), << (left shift) - these are native operators
 * - Bitwise functions: bitand(), bitor(), bitxor(), bitnot(), bitlshift(), bitrshift()
 * - Logical operators: && (and), || (or), & (and), | (or) - NOTE: & and | are LOGICAL, not bitwise!
 * - Mathematical functions: abs, sin, cos, sqrt, pow, etc.
 * - Parentheses for grouping
 *
 * **IMPORTANT BITWISE vs LOGICAL**:
 * - For bitwise operations on integers, use: bitand(), bitor(), bitxor(), bitnot()
 * - The & and | symbols are for LOGICAL (boolean) operations, not bitwise!
 * - Shift operators >> and << ARE native operators (not functions)
 *
 * **IMPORTANT PRECISION LIMITATION:**
 * Tinyexpr++ uses IEEE 754 double precision internally, which can only represent integers
 * exactly up to 2^53 - 1 (9,007,199,254,740,991). For uint64_t values exceeding this:
 * - Lower bits may be lost during conversion to double
 * - Bitwise operations may produce incorrect results
 * - A warning will be logged when this occurs
 * - @todo ASTL-286: Add uint64_t support for TinyExpr in ASTL.
 *
 * **Recommended Usage:**
 * - For values ≤ 2^53: ✅ Safe for all operations
 * - For values > 2^53: ⚠️ Extract bit fields in C++ code first, then apply formula
 * - For full uint64_t range: Consider pre-processing in integer domain
 *
 * Example expressions:
 * - "value * 0.001"                              // Scale by factor
 * - "bitand(value >> 8, 0xFF)"                   // Extract byte at position 8-15 (shift is native, bitand is function)
 * - "(value - 273.15) * 0.1"                     // Offset and scale
 * - "(bitand(value >> 4, 0xFF) - 50) * 0.5"      // Complex transformation: shift, mask, offset, scale
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
