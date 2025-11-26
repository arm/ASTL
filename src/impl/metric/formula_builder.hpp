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
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"
#include "metric/bit_mask_formula.hpp"
#include "metric/formula.hpp"
#include "metric/scaling_formula.hpp"

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
using AnyFormula = std::variant<IdentityFormula, BitMaskFormula, ScalingFormula>;

/**
 * @brief Apply a formula (from the variant) to a value.
 *
 * @param formula The formula to apply (NullFormula, BitMaskFormula, or ScalingFormula)
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
 * @brief Build a formula from JSON array configuration.
 *
 * Supports structured JSON array format only:
 * [{"operation": "BITMASK", "value": "0xFF"}]
 * or
 * [{"operation": "SCALING", "value": 0.001}]
 *
 * Currently only the first operation in the array is applied.
 * Future versions may support chaining multiple operations.
 *
 * @param formula_json JSON array containing formula operations
 * @return std::expected<AnyFormula, astl_status_code> The constructed formula or an error
 */
[[nodiscard]] auto BuildFormula(const nlohmann::json& formula_json) -> std::expected<AnyFormula, astl_status_code>;

}  // namespace astl

#endif  // FORMULA_BUILDER_HPP_
