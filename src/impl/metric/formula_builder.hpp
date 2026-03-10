// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef FORMULA_BUILDER_HPP_
#define FORMULA_BUILDER_HPP_

#include <expected>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"
#include "metric/expression_formula.hpp"
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
 * @brief Applies a sequence of formulas in order.
 */
class FormulaPipeline {
 public:
  // Allowed pipeline steps are the same concrete formulas that AnyFormula supports.
  using PipelineStep = std::variant<IdentityFormula, ScalingFormula, ExpressionFormula>;

  explicit FormulaPipeline(std::vector<PipelineStep> steps) : _steps(std::move(steps)) {}

  [[nodiscard]] auto Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> {
    AstlValue current = value;
    for (const auto& step : _steps) {
      auto result = std::visit([&current](const auto& impl) { return impl.Apply(current); }, step);
      if (!result) {
        return std::unexpected(result.error());
      }
      current = *result;
    }
    return current;
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static) - Must be non-static to satisfy Formula concept
  [[nodiscard]] auto Description() const -> std::string_view { return "PIPELINE"; }

  // Read-only accessor used by diagnostics and tests.
  [[nodiscard]] auto Steps() const -> std::vector<PipelineStep> const& { return _steps; }
  // Move accessor used by pipeline composition to avoid copying expression parser state.
  [[nodiscard]] auto TakeSteps() && -> std::vector<PipelineStep> { return std::move(_steps); }

 private:
  std::vector<PipelineStep> _steps;
};

static_assert(Formula<FormulaPipeline>, "FormulaPipeline does not satisfy Formula concept");

/**
 * @brief Variant type that can hold any supported formula type.
 */
using AnyFormula = std::variant<IdentityFormula, ScalingFormula, ExpressionFormula, FormulaPipeline>;

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
 *   "value / 1000"                         - Simple scaling
 *   "bitand(value, 0xFF)"                  - Bit masking (bitwise AND)
 *   "bitand(value >> 8, 0xFF)"             - Extract byte 1 (bits 8-15) - shift is native, bitand is function
 *   "(value - 273) / 10"                   - Offset and scale (integer-literal form)
 *   "bitand(value >> 4, 0xFF) / 2"         - Shift, mask, then scale
 *   "value << 4 | 0x0F"                    - Left shift and logical OR (not bitwise!)
 *
 * Non-integer numeric literals are rejected in configured formulas (e.g. 0.5,
 * 1e-3) to avoid TE_UINT64 truncation surprises for API consumers.
 *
 * @param formula_json The JSON value containing the formula configuration (optional)
 * @return std::expected<AnyFormula, astl_status_code> The formula or an error code
 */
auto BuildFormula(const std::optional<nlohmann::json>& formula_json) noexcept
    -> std::expected<AnyFormula, astl_status_code>;

/**
 * @brief Compose two formulas into a single executable formula.
 *
 * Identity formulas are removed, nested pipelines are flattened, and a
 * single-step pipeline is returned as its concrete formula type.
 *
 * @param first First formula to apply.
 * @param second Second formula to apply after @p first.
 * @return AnyFormula Composed formula preserving the execution order.
 */
auto ComposeFormulas(AnyFormula first, AnyFormula second) -> AnyFormula;

/**
 * @brief Render a formula into the public API formula text.
 *
 * This string is exposed through `astl_counter_properties_t::_formula` and is
 * intended to stay parseable by TinyExpr UINT64 clients. For base-10 scaling,
 * powers of ten are emitted as integer literals (e.g. `* 1000`, `/ 1000`).
 * Multi-step pipelines are flattened into one composable TinyExpr expression.
 *
 * @param formula Formula variant to render.
 * @return std::string User-facing formula string.
 */
auto FormatFormulaForApi(const AnyFormula& formula) -> std::string;

}  // namespace astl

#endif  // FORMULA_BUILDER_HPP_
