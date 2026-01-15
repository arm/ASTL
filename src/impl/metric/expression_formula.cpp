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

#include "metric/expression_formula.hpp"

#include <cmath>
#include <sstream>
#include <type_traits>

#include "astl_logger.hpp"
#include "tinyexpr.h"

namespace astl {

auto ExpressionFormula::Create(std::string expression) -> std::expected<ExpressionFormula, astl_status_code> {
  if (expression.empty()) {
    ASTL_LOG_ERROR("Expression formula cannot be empty");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  // Allocate variable storage on heap (needs to persist for compiled expression)
  auto input_value = std::make_unique<te_type>(0.0);

  // Create parser and bind the 'value' variable for use in expressions
  auto parser = std::make_unique<te_parser>();
  parser->set_variables_and_functions({
      {"value", input_value.get()}
  });

  // Compile the expression
  if (!parser->compile(expression)) {
    int64_t error_pos = parser->get_last_error_position();
    if (error_pos >= 0) {
      ASTL_LOG_ERROR("Failed to parse expression '{}' at position {}", expression, error_pos);
    } else {
      ASTL_LOG_ERROR("Failed to parse expression '{}'", expression);
    }
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  // Transfer ownership
  return ExpressionFormula(std::move(expression), std::move(parser), std::move(input_value));
}

ExpressionFormula::ExpressionFormula(std::string expression, std::unique_ptr<te_parser> parser,
                                     std::unique_ptr<te_type> input_value)
    : _expression(expression), _parser(std::move(parser)), _input_value(std::move(input_value)) {
  ASTL_LOG_DEBUG("Created ExpressionFormula: {}", _expression);
}

ExpressionFormula::ExpressionFormula(ExpressionFormula&& other) noexcept
    : _expression(std::move(other._expression)),
      _parser(std::move(other._parser)),
      _input_value(std::move(other._input_value)) {}

auto ExpressionFormula::operator=(ExpressionFormula&& other) noexcept -> ExpressionFormula& {
  if (this != &other) {
    _expression  = std::move(other._expression);
    _parser      = std::move(other._parser);
    _input_value = std::move(other._input_value);
  }
  return *this;
}

ExpressionFormula::~ExpressionFormula() = default;

namespace {
// Helper: Check for integer precision loss warning
template <typename T>
void CheckIntegerPrecision(T val, std::string_view expression) {
  constexpr uint64_t max_safe_integer = (1ULL << 53) - 1;  // 9,007,199,254,740,991
  uint64_t           abs_val          = 0;

  if constexpr (std::is_signed_v<T>) {
    abs_val = static_cast<uint64_t>(std::abs(static_cast<int64_t>(val)));
  } else {
    abs_val = static_cast<uint64_t>(val);
  }

  if (abs_val > max_safe_integer) {
    ASTL_LOG_WARNING(
        "Value {} exceeds safe integer range for double precision (2^53 = {}). "
        "Bitwise operations may lose precision. Expression: '{}'",
        abs_val, max_safe_integer, expression);
  }
}

// Helper: Validate expression evaluation result
auto ValidateResult(te_type result, std::string_view expression, auto input_val) -> std::optional<astl_status_code> {
  if (std::isnan(result)) {
    ASTL_LOG_ERROR("Expression '{}' evaluation resulted in NaN for input {}", expression, input_val);
    return ASTL_STATUS_INVALID_VALUE_TYPE;
  }

  if (std::isinf(result)) {
    ASTL_LOG_ERROR("Expression '{}' evaluation resulted in infinity for input {}", expression, input_val);
    return ASTL_STATUS_INVALID_VALUE_TYPE;
  }

  return std::nullopt;
}

// Helper: Convert result to AstlValue
template <typename T>
auto ConvertResult(te_type result) -> AstlValue {
  if constexpr (std::is_integral_v<T>) {
    // For integer inputs, round result to nearest integer
    constexpr double k_rounding_offset = 0.5;
    auto             rounded = static_cast<T>(result + (result >= 0 ? k_rounding_offset : -k_rounding_offset));
    return AstlValue{rounded};
  } else {
    // For floating point inputs, keep as double
    return AstlValue{static_cast<double>(result)};
  }
}
}  // namespace

auto ExpressionFormula::Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> {
  return std::visit(
      [this](auto&& val) -> std::expected<AstlValue, astl_status_code> {
        using T = std::decay_t<decltype(val)>;

        // Only support arithmetic types (not bool or string)
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
          // Check for potential precision loss with large integers
          if constexpr (std::is_integral_v<T>) {
            CheckIntegerPrecision(val, _expression);
          }

          // Set the variable value and evaluate
          *_input_value  = static_cast<te_type>(val);
          te_type result = _parser->evaluate();

          // Validate result
          if (auto error = ValidateResult(result, _expression, val)) {
            return std::unexpected(*error);
          }

          // Return result in appropriate type
          return ConvertResult<T>(result);
        } else {
          ASTL_LOG_ERROR("Expression formula does not support non-arithmetic types");
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      value.value);
}

}  // namespace astl
