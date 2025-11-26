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

#include "metric/formula_builder.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>

#include "astl_logger.hpp"
#include "astl_utils.hpp"

namespace astl {

// @todo ASTL-255: Support chaining multiple transformations together to form mathematical expressions.
// Currently, only one transformation  is supported.
//
// Examples of chained transformations:
//   - Apply bitmask (Register & 0xFFA0) followed by bit shift (>> 8)
//   - Apply scaling followed by offset adjustment
//   - Combine multiple operations: (value & mask) >> shift * scale + offset
// Future enhancement: Allow developers to specify formulas as mathematical expressions directly
//   - Example: "formula": "(value & 0xFFA0) >> 8 * 0.001"
//   - Support operators: &, |, ^, <<, >>, +, -, *, /, %, parentheses
//   - Parse and evaluate expression with proper operator precedence
//   - Would complement the structured array format with a more intuitive syntax
// - Support Q notation for fixed-point format as part of transformations.

auto BuildFormula(const nlohmann::json& formula_json) -> std::expected<AnyFormula, astl_status_code> {
  // Handle null or empty cases
  if (formula_json.is_null()) {
    return AnyFormula{IdentityFormula{}};
  }

  if (!formula_json.is_array()) {
    ASTL_LOG_ERROR("Formula must be a JSON array with operation objects");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  if (formula_json.empty()) {
    ASTL_LOG_WARNING("Empty formula array, treating as no formula");
    return AnyFormula{IdentityFormula{}};
  }

  if (formula_json.size() > 1) {
    ASTL_LOG_WARNING("Formula arrays with multiple transformations not yet supported, using first transformation only");
  }

  // Process first element
  const auto& first_op = formula_json[0];
  if (!first_op.is_object()) {
    ASTL_LOG_ERROR("Formula array element must be an object with 'transformation' and 'value' fields");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  if (!first_op.contains("transformation") || !first_op.contains("value")) {
    ASTL_LOG_ERROR("Formula object must contain 'transformation' and 'value' fields");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  std::string transformation = first_op["transformation"].get<std::string>();

  // Convert transformation to uppercase for validation
  std::string op_upper = transformation;
  std::transform(op_upper.begin(), op_upper.end(), op_upper.begin(), ::toupper);

  // BITMASK operations expect numeric value (e.g., 0xFF or 255)
  if (op_upper == "BITMASK") {
    if (!first_op["value"].is_number()) {
      ASTL_LOG_ERROR("BITMASK formula 'value' must be a number (e.g., 0xFF or 255)");
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    uint64_t mask_value = first_op["value"].get<uint64_t>();
    return AnyFormula{BitMaskFormula{mask_value}};
  }

  // SCALING operations expect numeric value (e.g., 0.001 or 1.5)
  if (op_upper == "SCALING") {
    if (!first_op["value"].is_number()) {
      ASTL_LOG_ERROR("SCALING formula 'value' must be a number (e.g., 0.001 or 1.5)");
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    double scale_factor = first_op["value"].get<double>();
    return AnyFormula{ScalingFormula{scale_factor}};
  }

  ASTL_LOG_ERROR("Unknown formula transformation: '{}'", op_upper);
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

}  // namespace astl
