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

#include "common/astl_value.hpp"

#include <compare>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <string>
#include <variant>

#include "astl_logger.hpp"

namespace astl {

AstlValue::AstlValue(uint8_t val) : value{val} {}
AstlValue::AstlValue(uint16_t val) : value{val} {}
AstlValue::AstlValue(uint32_t val) : value{val} {}
AstlValue::AstlValue(uint64_t val) : value{val} {}
AstlValue::AstlValue(float val) : value{val} {}
AstlValue::AstlValue(double val) : value{val} {}
AstlValue::AstlValue(bool val) : value{val} {}

auto AstlValue::operator==(const AstlValue& other) const -> bool {
  // first, handle cases where one or both variants are valueless_by_exception
  if (value.valueless_by_exception() && other.value.valueless_by_exception()) {
    return true;
  }
  if (value.valueless_by_exception() || other.value.valueless_by_exception()) {
    return false;
  }

  const auto is_equal = [](const auto& lhs, const auto& rhs) -> bool {
    using L = std::decay_t<decltype(lhs)>;
    using R = std::decay_t<decltype(rhs)>;

    constexpr bool lhs_is_floating = std::is_floating_point_v<L>;
    constexpr bool rhs_is_floating = std::is_floating_point_v<R>;

    // Policy: integral-vs-floating comparisons are never equal.
    if constexpr (lhs_is_floating != rhs_is_floating) {
      return false;
    }

    if constexpr (lhs_is_floating && rhs_is_floating) {
      return static_cast<long double>(lhs) == static_cast<long double>(rhs);
    }

    // Integral family (including bool): compare numerically.
    return static_cast<uint64_t>(lhs) == static_cast<uint64_t>(rhs);
  };
  return std::visit(is_equal, value, other.value);
}

auto AstlValue::operator<=>(const AstlValue& other) const -> std::partial_ordering {
  // first, handle valueless-by-exception edge cases
  if (value.valueless_by_exception() && other.value.valueless_by_exception()) {
    return std::partial_ordering::equivalent;
  }
  if (value.valueless_by_exception()) {
    return std::partial_ordering::less;
  }
  if (other.value.valueless_by_exception()) {
    return std::partial_ordering::greater;
  }

  const auto spaceship = [](const auto& lhs, const auto& rhs) -> std::partial_ordering {
    using L = std::decay_t<decltype(lhs)>;
    using R = std::decay_t<decltype(rhs)>;

    constexpr bool lhs_is_floating = std::is_floating_point_v<L>;
    constexpr bool rhs_is_floating = std::is_floating_point_v<R>;

    // Policy: impose deterministic category ordering for mixed integral/floating comparisons.
    if constexpr (lhs_is_floating != rhs_is_floating) {
      return lhs_is_floating ? std::partial_ordering::greater : std::partial_ordering::less;
    }

    if constexpr (lhs_is_floating && rhs_is_floating) {
      return static_cast<long double>(lhs) <=> static_cast<long double>(rhs);
    }
    // Integral family (including bool): compare numerically.
    return static_cast<uint64_t>(lhs) <=> static_cast<uint64_t>(rhs);
  };
  return std::visit(spaceship, value, other.value);
}

/**
 * @brief convert a C-style astl_value_t to a AstlValue according to the specified astl_value_type_t
 *
 * @return an AstlValue instance with the same value as val, or a ASTL_STATUS_INVALID_VALUE_TYPE
 */
auto AstlValue::FromUnion(const astl_value_t& val, astl_value_type_t type)
    -> std::expected<AstlValue, astl_status_code> {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{val.ui8};
    case ASTL_VALUE_UINT16:      return AstlValue{val.ui16};
    case ASTL_VALUE_UINT32:      return AstlValue{val.ui32};
    case ASTL_VALUE_UINT64:      return AstlValue{val.ui64};
    case ASTL_VALUE_FLOAT32:     return AstlValue{val.fp32};
    case ASTL_VALUE_FLOAT64:     return AstlValue{val.fp64};
    case ASTL_VALUE_BOOL8:       return AstlValue{val.b8};
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

auto AstlValue::FromUnionPromoting(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code> {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:     return AstlValue{uint64_t{0}};
    case ASTL_VALUE_UINT16:    return AstlValue{uint64_t{0}};
    case ASTL_VALUE_UINT32:    return AstlValue{uint64_t{0}};
    case ASTL_VALUE_UINT64:    return AstlValue{uint64_t{0}};
    case ASTL_VALUE_FLOAT32:   return AstlValue{0.0};
    case ASTL_VALUE_FLOAT64:   return AstlValue{0.0};
    case ASTL_VALUE_BOOL8:     return AstlValue{uint64_t{0}};
    case ASTL_VALUE_UNKNOWN:   return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                   return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief Create an AstlValue of the given type with the minimal possible value
 *
 * @return an AstlValue instance with the minimum possible value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
 */
auto AstlValue::FromMinimum(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code> {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{std::numeric_limits<uint8_t>::min()};
    case ASTL_VALUE_UINT16:      return AstlValue{std::numeric_limits<uint16_t>::min()};
    case ASTL_VALUE_UINT32:      return AstlValue{std::numeric_limits<uint32_t>::min()};
    case ASTL_VALUE_UINT64:      return AstlValue{std::numeric_limits<uint64_t>::min()};
    case ASTL_VALUE_FLOAT32:     return AstlValue{std::numeric_limits<float>::lowest()};
    case ASTL_VALUE_FLOAT64:     return AstlValue{std::numeric_limits<double>::lowest()};
    case ASTL_VALUE_BOOL8:       return AstlValue{std::numeric_limits<bool>::min()};
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief Create an AstlValue of the given type as close to '0' as possible
 *
 * @return an AstlValue instance of 0 value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
 */
auto AstlValue::FromZero(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code> {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{uint8_t{0}};
    case ASTL_VALUE_UINT16:      return AstlValue{uint16_t{0}};
    case ASTL_VALUE_UINT32:      return AstlValue{uint32_t{0}};
    case ASTL_VALUE_UINT64:      return AstlValue{uint64_t{0}};
    case ASTL_VALUE_FLOAT32:     return AstlValue{0.0F};
    case ASTL_VALUE_FLOAT64:     return AstlValue{0.0};
    case ASTL_VALUE_BOOL8:       return AstlValue{false};
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief Create an AstlValue of the given type with the maximum possible value
 *
 * @return an AstlValue instance with the max representable value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
 */
auto AstlValue::FromMaximum(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code> {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{std::numeric_limits<uint8_t>::max()};
    case ASTL_VALUE_UINT16:      return AstlValue{std::numeric_limits<uint16_t>::max()};
    case ASTL_VALUE_UINT32:      return AstlValue{std::numeric_limits<uint32_t>::max()};
    case ASTL_VALUE_UINT64:      return AstlValue{std::numeric_limits<uint64_t>::max()};
    case ASTL_VALUE_FLOAT32:     return AstlValue{std::numeric_limits<float>::max()};
    case ASTL_VALUE_FLOAT64:     return AstlValue{std::numeric_limits<double>::max()};
    case ASTL_VALUE_BOOL8:       return AstlValue{std::numeric_limits<bool>::max()};
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief convert the C++ style AstlValue to a pair of C API astl_value_t and astl_value_type_t
 *
 * @return a std::pair where the first element is the value and the second idenfifies the type
 */
auto AstlValue::ToAstlUnionValue() const -> std::pair<astl_value_t, astl_value_type_t> {
  astl_value_t      union_val{};
  astl_value_type_t type{ASTL_VALUE_UNKNOWN};

  try {
    // clang-format off
    std::visit([&](auto&& arg) {
      using T = std::decay_t<decltype(arg)>;
      if      constexpr (std::is_same_v<T, uint8_t>)  { union_val.ui8  = arg; type = ASTL_VALUE_UINT8; }
      else if constexpr (std::is_same_v<T, uint16_t>) { union_val.ui16 = arg; type = ASTL_VALUE_UINT16; }
      else if constexpr (std::is_same_v<T, uint32_t>) { union_val.ui32 = arg; type = ASTL_VALUE_UINT32; }
      else if constexpr (std::is_same_v<T, uint64_t>) { union_val.ui64 = arg; type = ASTL_VALUE_UINT64; }
      else if constexpr (std::is_same_v<T, float>)    { union_val.fp32 = arg; type = ASTL_VALUE_FLOAT32; }
      else if constexpr (std::is_same_v<T, double>)   { union_val.fp64 = arg; type = ASTL_VALUE_FLOAT64; }
      else if constexpr (std::is_same_v<T, bool>)     { union_val.b8   = arg; type = ASTL_VALUE_BOOL8; }
    }, value);
  } catch (const std::bad_variant_access& e) {
    ASTL_LOG_ERROR("Failed to convert AstlValue to astl_value_t: {}", e.what());
    return {union_val, type};  // return default unknown value on error.
  }
  // clang-format on
  return {union_val, type};
}

/**
 * @brief Compute the sum of two AstlValues of the same underlying type.
 *
 * @return value of common larger type of the two operands or a status code:
 *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
 *   - ASTL_STATUS_INVALID_VALUE_TYPE if operands aren't similar types
 */
auto AstlValue::Add(const AstlValue& addend, const AstlValue& augend) -> std::expected<AstlValue, astl_status_code> {
  return std::visit(
      [](auto&& left, auto&& right) -> std::expected<AstlValue, astl_status_code> {
        // X is the type of the left, but promote bool to uint8_t to avoid unsafe arithmetic
        using X = std::conditional_t<std::is_same_v<std::decay_t<decltype(left)>, bool>, uint8_t,
                                     std::decay_t<decltype(left)>>;
        // Y is the type of the right, but promote bool to uint8_t to avoid unsafe arithmetic
        using Y = std::conditional_t<std::is_same_v<std::decay_t<decltype(right)>, bool>, uint8_t,
                                     std::decay_t<decltype(right)>>;

        // disallow adding integral types with floating point types
        if constexpr ((std::is_integral_v<X> && !std::is_integral_v<Y>) ||
                      (!std::is_integral_v<X> && std::is_integral_v<Y>)) {
          ASTL_LOG_ERROR("Cannot add integral type with floating point type");
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }

        if constexpr (std::is_arithmetic_v<X> && std::is_arithmetic_v<Y>) {
          // Cast both operands to common type
          using Result = std::conditional_t<(sizeof(X) >= sizeof(Y)), X, Y>;

          Result left_cast  = static_cast<Result>(left);
          Result right_cast = static_cast<Result>(right);

          // Check for overflow (only for unsigned integers)
          if constexpr (std::is_integral_v<Result> && std::is_unsigned_v<Result>) {
            if ((std::numeric_limits<Result>::max() - left_cast) < right_cast) [[unlikely]] {
              return std::unexpected(ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
            }
          }
          Result sum{static_cast<Result>(left_cast + right_cast)};
          return AstlValue{sum};
        } else {
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      addend.value, augend.value);
}

/**
 * @brief Compute the difference of two AstlValues of the same underlying type.
 *
 * @return value of common larger type of the two operands or a status code:
 *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
 *   - ASTL_STATUS_INVALID_VALUE_TYPE if operands aren't similar types
 */
auto AstlValue::Subtract(const AstlValue& minuend, const AstlValue& subtrahend)
    -> std::expected<AstlValue, astl_status_code> {
  // Helper performs unsigned underflow check; bool is promoted to uint8_t first to avoid unsafe comparisons.
  auto underflow_check = [](auto lhs, auto rhs) -> bool {
    // T is the lhs type, but promote bool to uint8_t to avoid unsafe arithmetic
    using T =
        std::conditional_t<std::is_same_v<std::decay_t<decltype(lhs)>, bool>, uint8_t, std::decay_t<decltype(lhs)>>;
    if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
      return lhs < rhs;
    }
    return false;  // no underflow for signed or floating point types
  };

  return std::visit(
      [&underflow_check](auto&& left, auto&& right) -> std::expected<AstlValue, astl_status_code> {
        using LX = std::decay_t<decltype(left)>;
        using RX = std::decay_t<decltype(right)>;
        if constexpr (std::is_arithmetic_v<LX> && std::is_arithmetic_v<RX>) {
          using Result      = std::conditional_t<(sizeof(LX) >= sizeof(RX)), LX, RX>;
          Result left_cast  = static_cast<Result>(left);
          Result right_cast = static_cast<Result>(right);

          if (underflow_check(left_cast, right_cast)) [[unlikely]] {
            return std::unexpected(ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
          }

          Result difference = static_cast<Result>(left_cast - right_cast);
          return AstlValue{difference};
        } else {
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      minuend.value, subtrahend.value);
}

/**
 * @brief convert the AstlValue to a string suitable for Log debugging
 */
auto to_string(const AstlValue& variant_value) -> std::string {
  return std::visit(
      [](const auto& visited_value) -> std::string {
        using T = std::decay_t<decltype(visited_value)>;
        if constexpr (std::is_same_v<T, bool>) {
          return visited_value ? "true" : "false";
        } else {
          return std::format("{}", visited_value);
        }
      },
      variant_value.value);
}

}  // namespace astl
