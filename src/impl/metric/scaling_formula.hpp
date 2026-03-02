// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCALING_FORMULA_HPP_
#define SCALING_FORMULA_HPP_

#include <expected>
#include <sstream>
#include <string>
#include <type_traits>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"
#include "metric/formula.hpp"

namespace astl {

/**
 * @brief Formula that multiplies values by a scaling factor.
 *
 * This formula is useful for unit conversions (e.g., raw counts to watts).
 * Supports all arithmetic types except bool.
 */
class ScalingFormula {
 public:
  explicit ScalingFormula(double scale_factor) : _scale_factor(scale_factor) {
    // Pre-compute description string
    std::ostringstream oss;
    oss << "SCALING " << _scale_factor;
    _description = oss.str();
  }

  [[nodiscard]] auto Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> {
    return std::visit(
        [this](auto&& val) -> std::expected<AstlValue, astl_status_code> {
          using T = std::decay_t<decltype(val)>;

          // Only support arithmetic types (not bool)
          if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            // Perform scaling in double precision
            double scaled = static_cast<double>(val) * _scale_factor;

            // For integer types, round to nearest integer
            if constexpr (std::is_integral_v<T>) {
              constexpr double k_rounding_offset = 0.5;
              T                result = static_cast<T>(scaled + (scaled >= 0 ? k_rounding_offset : -k_rounding_offset));
              return AstlValue{result};
            } else {
              // For floating point types, return as-is
              return AstlValue{static_cast<T>(scaled)};
            }
          } else {
            return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
          }
        },
        value.value);
  }

  [[nodiscard]] auto Description() const -> std::string_view { return _description; }

  [[nodiscard]] auto GetScaleFactor() const -> double { return _scale_factor; }

 private:
  double      _scale_factor{1.0};
  std::string _description;
};

static_assert(Formula<ScalingFormula>, "ScalingFormula does not satisfy Formula concept");

}  // namespace astl

#endif  // SCALING_FORMULA_HPP_
