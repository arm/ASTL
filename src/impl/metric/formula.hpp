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

#ifndef FORMULA_HPP_
#define FORMULA_HPP_

#include <expected>
#include <string_view>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"

namespace astl {

/**
 * @brief Concept for formula types that can be applied to values.
 *
 * Formulas are applied to raw metric samples to transform them before processing.
 * Examples: ExpressionFormula evaluates expressions including bitwise operations.
 *
 * This concept defines the interface that all formula types must satisfy:
 * - Apply(const AstlValue&) -> std::expected<AstlValue, astl_status_code>
 * - Description() const -> std::string_view
 */
template <typename T>
concept Formula = requires(const T& formula, const AstlValue& value) {
  { formula.Apply(value) } -> std::same_as<std::expected<AstlValue, astl_status_code>>;
  { formula.Description() } -> std::same_as<std::string_view>;
};

}  // namespace astl

#endif  // FORMULA_HPP_
