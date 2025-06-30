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

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <variant>

namespace astl {

AstlValue::AstlValue(uint8_t val) : value{val} {}
AstlValue::AstlValue(uint16_t val) : value{val} {}
AstlValue::AstlValue(uint32_t val) : value{val} {}
AstlValue::AstlValue(uint64_t val) : value{val} {}
AstlValue::AstlValue(float val) : value{val} {}
AstlValue::AstlValue(double val) : value{val} {}
AstlValue::AstlValue(bool val) : value{val} {}
AstlValue::AstlValue(std::string val) : value{std::move(val)} {}

/**
 * @brief convert a C-style astl_value_t to a AstlValue according to the specified astl_value_type_t
 *
 * @return an AstlValue instance with the same value as val, or a ASTL_STATUS_INVALID_VALUE_TYPE
 */
std::expected<AstlValue, astl_status_code> AstlValue::FromUnion(const astl_value_t& val, astl_value_type_t type) {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{val.ui8};
    case ASTL_VALUE_UINT16:      return AstlValue{val.ui16};
    case ASTL_VALUE_UINT32:      return AstlValue{val.ui32};
    case ASTL_VALUE_UINT64:      return AstlValue{val.ui64};
    case ASTL_VALUE_FLOAT32:     return AstlValue{val.fp32};
    case ASTL_VALUE_FLOAT64:     return AstlValue{val.fp64};
    case ASTL_VALUE_BOOL8:       return AstlValue{val.b8};
    case ASTL_VALUE_STRING:      return AstlValue{std::string(val.str ? val.str : "")};
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief Create an AstlValue of the given type with the minimal possible value
 *
 * @return an AstlValue instance with the minimum possible value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
 */
std::expected<AstlValue, astl_status_code> AstlValue::FromMinimum(astl_value_type_t type) {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{std::numeric_limits<uint8_t>::min()};
    case ASTL_VALUE_UINT16:      return AstlValue{std::numeric_limits<uint16_t>::min()};
    case ASTL_VALUE_UINT32:      return AstlValue{std::numeric_limits<uint32_t>::min()};
    case ASTL_VALUE_UINT64:      return AstlValue{std::numeric_limits<uint64_t>::min()};
    case ASTL_VALUE_FLOAT32:     return AstlValue{std::numeric_limits<float>::lowest()};
    case ASTL_VALUE_FLOAT64:     return AstlValue{std::numeric_limits<double>::lowest()};
    case ASTL_VALUE_BOOL8:       return AstlValue{std::numeric_limits<bool>::min()};
    case ASTL_VALUE_STRING:      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief Create an AstlValue of the given type with the maximum possible value
 *
 * @return an AstlValue instance with the max representable value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
 * @note if type == ASTL_VALUE_STRING, this returns ASTL_STATUS_INVALID_VALUE_TYPE
 */
std::expected<AstlValue, astl_status_code> AstlValue::FromMaximum(astl_value_type_t type) {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return AstlValue{std::numeric_limits<uint8_t>::max()};
    case ASTL_VALUE_UINT16:      return AstlValue{std::numeric_limits<uint16_t>::max()};
    case ASTL_VALUE_UINT32:      return AstlValue{std::numeric_limits<uint32_t>::max()};
    case ASTL_VALUE_UINT64:      return AstlValue{std::numeric_limits<uint64_t>::max()};
    case ASTL_VALUE_FLOAT32:     return AstlValue{std::numeric_limits<float>::max()};
    case ASTL_VALUE_FLOAT64:     return AstlValue{std::numeric_limits<double>::max()};
    case ASTL_VALUE_BOOL8:       return AstlValue{std::numeric_limits<bool>::max()};
    case ASTL_VALUE_STRING:      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief convert the C++ style AstlValue to a pair of C API astl_value_t and astl_value_type_t
 *
 * @return a std::pair where the first element is the value and the second idenfifies the type
 * @note if the AstlValue is of type std::string, the returned astl_value_t will have a char *
 *       pointing to the AstlValue's internal string buffer. So the AstlValue must outlive the returned
 *       astl_value_t.
 */
std::pair<astl_value_t, astl_value_type_t> AstlValue::ToAstlUnionValue() const {
  astl_value_t      union_val{};
  astl_value_type_t type{ASTL_VALUE_UNKNOWN};

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
      else if constexpr (std::is_same_v<T, std::string>) {
        union_val.str = arg.c_str(); // only valid if lifetime is managed externally
        type = ASTL_VALUE_STRING;
      }
    }, value);
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
std::expected<AstlValue, astl_status_code> AstlValue::Add(const AstlValue& addend, const AstlValue& augend) {
  return std::visit(
      [](auto&& left, auto&& right) -> std::expected<AstlValue, astl_status_code> {
        using X = std::decay_t<decltype(left)>;
        using Y = std::decay_t<decltype(right)>;

        if constexpr (std::is_arithmetic_v<X> && std::is_arithmetic_v<Y>) {
          // Cast both operands to common type
          using Result      = std::conditional_t<(sizeof(X) >= sizeof(Y)), X, Y>;
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
        } else if constexpr (std::is_same_v<X, std::string> && std::is_same_v<Y, std::string>) {
          return AstlValue(left + right);  // string concatenation
        } else {
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      addend.value, augend.value);
}

/**
 * @brief convert the AstlValue to a string suitable for Log debugging
 */
std::string to_string(const AstlValue& variant_value) {
  return std::visit(
      [](const auto& visited_value) -> std::string {
        using T = std::decay_t<decltype(visited_value)>;
        if constexpr (std::is_same_v<T, bool>) {
          return visited_value ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
          return visited_value;
        } else {
          return std::format("{}", visited_value);
        }
      },
      variant_value.value);
}

}  // namespace astl
