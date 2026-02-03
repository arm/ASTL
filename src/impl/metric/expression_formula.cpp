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
  auto input_value = std::make_unique<te_type>(0);

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

auto ExpressionFormula::Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> {
  return std::visit(
      [this](auto&& val) -> std::expected<AstlValue, astl_status_code> {
        using T = std::decay_t<decltype(val)>;

        // Only support arithmetic types (not bool or string)
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
          // Warn if floating-point input is used with TE_UINT64 mode
          if constexpr (std::is_floating_point_v<T>) {
            ASTL_LOG_WARNING(
                "Floating-point input value {} will be truncated to uint64_t for expression '{}'. "
                "Consider using integer types for TE_UINT64 mode.",
                val, _expression);
          }

          // Set the variable value and evaluate
          *_input_value  = static_cast<te_type>(val);
          te_type result = _parser->evaluate();

          // Return result cast back to input type
          return AstlValue{static_cast<T>(result)};
        } else {
          ASTL_LOG_ERROR("Expression formula does not support non-arithmetic types");
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      value.value);
}

}  // namespace astl
