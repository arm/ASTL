// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCALING_FORMULA_HPP_
#define SCALING_FORMULA_HPP_

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>

#include "astl/astl_errors.h"
#include "astl_internal_status.hpp"
#include "common/astl_value.hpp"
#include "metric/formula.hpp"

namespace astl {

// GCC generates a false-positive -Wmaybe-uninitialized when ScalingFormula is
// moved as part of a std::variant move constructor. The two uint64_t members
// (_numerator, _denominator) are always initialised via default member
// initialisers, but the compiler loses track of that when it vectorises the
// pair into a 128-bit load inside the deeply-inlined variant machinery.
// The guard excludes Clang, which defines __GNUC__ for compatibility but does
// not recognise this warning group and would error on the unknown option.
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

/**
 * @brief Formula that scales values by an exact rational factor (numerator/denominator).
 *
 * This formula is useful for unit conversions (e.g., raw counts to watts).
 * Internal execution preserves fractional results by computing:
 * `static_cast<double>(value * numerator) / denominator`.
 * For integral inputs, multiply is overflow-checked before conversion.
 * Supports all arithmetic types except bool and returns a float64 value.
 */
class ScalingFormula {
 public:
  explicit ScalingFormula(uint64_t numerator, uint64_t denominator = 1)
      : _numerator(numerator), _denominator(denominator) {
    // Canonicalize the ratio so runtime behavior and rendered formulas are stable.
    if (_denominator != 0 && _numerator != 0) {
      const auto divisor = std::gcd(_numerator, _denominator);
      _numerator /= divisor;
      _denominator /= divisor;
    }
    if (_numerator == 0) {
      _denominator = 1;
    }
    RefreshDescription();
  }

  [[nodiscard]] auto Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> {
    return std::visit(
        [this](auto&& val) -> std::expected<AstlValue, astl_status_code> {
          using T = std::decay_t<decltype(val)>;

          // Only support arithmetic types (not bool)
          if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            if (_denominator == 0) {
              return std::unexpected(astl::kInternalDivideByZero);
            }
            if constexpr (std::is_integral_v<T>) {
              const auto raw_value = static_cast<uint64_t>(val);
              // Make overflow behavior explicit instead of silently wrapping the multiplication.
              if (_numerator != 0 && raw_value > std::numeric_limits<uint64_t>::max() / _numerator) {
                return std::unexpected(ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
              }
              const uint64_t product = raw_value * _numerator;
              const double   scaled  = static_cast<double>(product) / static_cast<double>(_denominator);
              return AstlValue{scaled};
            } else {
              const double scaled =
                  (static_cast<double>(val) * static_cast<double>(_numerator)) / static_cast<double>(_denominator);
              if (!std::isfinite(scaled)) {
                return std::unexpected(ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
              }
              return AstlValue{scaled};
            }
          } else {
            return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
          }
        },
        value.value);
  }

  [[nodiscard]] auto Description() const -> std::string_view { return _description; }

  [[nodiscard]] auto GetNumerator() const -> uint64_t { return _numerator; }
  [[nodiscard]] auto GetDenominator() const -> uint64_t { return _denominator; }

 private:
  auto RefreshDescription() -> void {
    std::ostringstream oss;
    oss << "SCALING " << _numerator << "/" << _denominator;
    _description = oss.str();
  }

  uint64_t    _numerator{1};
  uint64_t    _denominator{1};
  std::string _description;
};

static_assert(Formula<ScalingFormula>, "ScalingFormula does not satisfy Formula concept");

#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif

}  // namespace astl

#endif  // SCALING_FORMULA_HPP_
