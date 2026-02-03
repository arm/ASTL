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

#ifndef FORMULA_BUILDER_HPP_
#define FORMULA_BUILDER_HPP_

#include <expected>
#include <nlohmann/json.hpp>
#include <string_view>
#include <variant>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"
#include "metric/expression_formula.hpp"
#include "metric/formula.hpp"

namespace astl {

/**
 * @brief A no-op formula that returns the input value unchanged.
 *
 * Used when no formula is specified in the configuration.
 */
class IdentityFormula {
 public:
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) - Must be non-static to satisfy Formula concept
  [[nodiscard]] auto Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> { return value; }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) - Must be non-static to satisfy Formula concept
  [[nodiscard]] auto Description() const -> std::string_view { return "NONE"; }
};

static_assert(Formula<IdentityFormula>, "IdentityFormula does not satisfy Formula concept");

/**
 * @brief Variant type that can hold any supported formula type.
 */
using AnyFormula = std::variant<IdentityFormula, ExpressionFormula>;

/**
 * @brief Apply a formula (from the variant) to a value.
 *
 * @param formula The formula to apply (IdentityFormula or ExpressionFormula)
 * @param value The value to transform
 * @return std::expected<AstlValue, astl_status_code> The transformed value or an error
 */
inline auto ApplyFormula(const AnyFormula& formula, const AstlValue& value)
    -> std::expected<AstlValue, astl_status_code> {
  return std::visit([&value](const auto& formula_impl) { return formula_impl.Apply(value); }, formula);
}

/**
 * @brief Get a description of the formula for debugging/logging.
 *
 * @return std::string_view A view into the formula description (does not own the string)
 * @note The returned view is valid as long as the formula object exists
 */
inline auto GetFormulaDescription(const AnyFormula& formula) -> std::string_view {
  return std::visit([](const auto& formula_impl) { return formula_impl.Description(); }, formula);
}

/**
 * @brief Build a formula from JSON configuration.
 *
 * Parses the formula configuration and creates the appropriate formula type:
 * - null or empty string → IdentityFormula (no transformation)
 * - Any string expression → ExpressionFormula (mathematical expression using tinyexpr++)
 *
 * String expressions support:
 * - Arithmetic operators: +, -, *, /, %
 * - Shift operators: >> (right shift), << (left shift)
 * - Bitwise functions: bitand(), bitor(), bitxor(), bitnot(), bitlshift(), bitrshift()
 * - Logical operators: && (and), || (or), & (and), | (or) - NOTE: & and | are LOGICAL, not bitwise!
 * - Mathematical functions: abs, sin, cos, sqrt, pow, ln, log, exp, etc.
 * - Constants: pi(), e()
 * - Parentheses for grouping
 * - Variable 'value' represents the input value
 *
 * **IMPORTANT**: For bitwise AND/OR/XOR, use functions `bitand()`, `bitor()`, `bitxor()`.
 * The `&` and `|` operators are for LOGICAL operations (boolean), not bitwise!
 *
 * Examples:
 *   "value * 0.001"                        - Simple scaling
 *   "bitand(value, 0xFF)"                  - Bit masking (bitwise AND)
 *   "bitand(value >> 8, 0xFF)"             - Extract byte 1 (bits 8-15) - shift is native, bitand is function
 *   "(value - 273.15) * 0.1"               - Offset and scale
 *   "bitand(value >> 4, 0xFF) * 0.5"       - Shift, mask, then scale
 *   "value << 4 | 0x0F"                    - Left shift and logical OR (not bitwise!)
 *
 * @param formula_json The JSON value containing the formula configuration (optional)
 * @return std::expected<AnyFormula, astl_status_code> The formula or an error code
 */
auto BuildFormula(const std::optional<nlohmann::json>& formula_json) -> std::expected<AnyFormula, astl_status_code>;

}  // namespace astl

#endif  // FORMULA_BUILDER_HPP_
