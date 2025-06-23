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

#ifndef ASTL_VALUE_HPP_
#define ASTL_VALUE_HPP_

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <variant>

#include "astl/astl.h"

namespace astl {

/**
 * @brief A C++ representation of the API-level astl_value_t.
 * Using variant to identify which of the astl_value_type_t possibilities this holds
 * @note the set of possible variants should be equivalent to the union members of astl_value_t.
 */
using AstlValue = std::variant<uint8_t, uint16_t, uint32_t, uint64_t, float, double, bool, std::string>;

/**
 * @brief convert a C-style astl_value_t to a AstlValue according to the specified astl_value_type_t
 *
 * @return an AstlValue instance with the same value as val, or a ASTL_STATUS_INTERNAL_ERROR
 */
std::expected<AstlValue, astl_status_code> ToVariant(const astl_value_t& val, astl_value_type_t type) {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return val.ui8;
    case ASTL_VALUE_UINT16:      return val.ui16;
    case ASTL_VALUE_UINT32:      return val.ui32;
    case ASTL_VALUE_UINT64:      return val.ui64;
    case ASTL_VALUE_FLOAT32:     return val.fp32;
    case ASTL_VALUE_FLOAT64:     return val.fp64;
    case ASTL_VALUE_BOOL8:       return val.b8;
    case ASTL_VALUE_STRING:      return std::string(val.str ? val.str : "");
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/** @brief constructor tag type indicating we should make a AstlValue of the lowest value*/
struct AstlValueMin {};
/** @brief constructor tag type indicating we should make a AstlValue of the maximum value*/
struct AstlValueMax {};

/**
 * @brief Create an AstlValue of the given type with the minimal possible value
 *
 * @return an AstlValue instance with the minimum possible value of `type`, or a ASTL_STATUS_INTERNAL_ERROR
 */
std::expected<AstlValue, astl_status_code> ToVariant(AstlValueMin /*tag*/, astl_value_type_t type) {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return std::numeric_limits<uint8_t>::min();
    case ASTL_VALUE_UINT16:      return std::numeric_limits<uint16_t>::min();
    case ASTL_VALUE_UINT32:      return std::numeric_limits<uint32_t>::min();
    case ASTL_VALUE_UINT64:      return std::numeric_limits<uint64_t>::min();
    case ASTL_VALUE_FLOAT32:     return std::numeric_limits<float>::lowest();
    case ASTL_VALUE_FLOAT64:     return std::numeric_limits<double>::lowest();
    case ASTL_VALUE_BOOL8:       return std::numeric_limits<bool>::min();
    case ASTL_VALUE_STRING:      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    case ASTL_VALUE_UNKNOWN:     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    default:                     return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
  // clang-format on
}

/**
 * @brief Create an AstlValue of the given type with the maximum possible value
 *
 * @return an AstlValue instance with the max representable value of `type`, or a ASTL_STATUS_INTERNAL_ERROR
 * @note if type == ASTL_VALUE_STRING, this returns ASTL_STATUS_INTERNAL_ERROR
 */
std::expected<AstlValue, astl_status_code> ToVariant(AstlValueMax /*tag*/, astl_value_type_t type) {
  // clang-format off
  switch (type) {
    case ASTL_VALUE_UINT8:       return std::numeric_limits<uint8_t>::max();
    case ASTL_VALUE_UINT16:      return std::numeric_limits<uint16_t>::max();
    case ASTL_VALUE_UINT32:      return std::numeric_limits<uint32_t>::max();
    case ASTL_VALUE_UINT64:      return std::numeric_limits<uint64_t>::max();
    case ASTL_VALUE_FLOAT32:     return std::numeric_limits<float>::max();
    case ASTL_VALUE_FLOAT64:     return std::numeric_limits<double>::max();
    case ASTL_VALUE_BOOL8:       return std::numeric_limits<bool>::max();
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
std::pair<astl_value_t, astl_value_type_t> ToAstlUnionValue(const AstlValue& var) {
  astl_value_t      val{};
  astl_value_type_t type{ASTL_VALUE_UNKNOWN};

  // clang-format off
  std::visit([&](auto&& arg) {
    using T = std::decay_t<decltype(arg)>;
    if      constexpr (std::is_same_v<T, uint8_t>)  { val.ui8  = arg; type = ASTL_VALUE_UINT8; }
    else if constexpr (std::is_same_v<T, uint16_t>) { val.ui16 = arg; type = ASTL_VALUE_UINT16; }
    else if constexpr (std::is_same_v<T, uint32_t>) { val.ui32 = arg; type = ASTL_VALUE_UINT32; }
    else if constexpr (std::is_same_v<T, uint64_t>) { val.ui64 = arg; type = ASTL_VALUE_UINT64; }
    else if constexpr (std::is_same_v<T, float>)    { val.fp32 = arg; type = ASTL_VALUE_FLOAT32; }
    else if constexpr (std::is_same_v<T, double>)   { val.fp64 = arg; type = ASTL_VALUE_FLOAT64; }
    else if constexpr (std::is_same_v<T, bool>)     { val.b8   = arg; type = ASTL_VALUE_BOOL8; }
    else if constexpr (std::is_same_v<T, std::string>) {
      val.str = const_cast<char*>(arg.c_str()); // only valid if lifetime is managed externally
      type = ASTL_VALUE_STRING;
    }
  }, var);
  // clang-format on
  return {val, type};
}

/**
 * @brief Compute the sum of two AstlValues of the same underlying type.
 *
 * @return value of same type as a and b or a status code:
 *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
 *   - ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE if a and b are not the same arithmetic type
 */
std::expected<AstlValue, astl_status_code> Add(const AstlValue& addend, const AstlValue& augend) {
  return std::visit(
      [](auto&& left, auto&& right) -> std::expected<AstlValue, astl_status_code> {
        using X = std::decay_t<decltype(left)>;
        using Y = std::decay_t<decltype(right)>;

        // constrain compilation of this section to cases where type X == type Y
        // and both are arithmetic.
        if constexpr (std::is_same_v<X, Y> && std::is_arithmetic_v<X>) {
          // check for overflow
          if ((std::numeric_limits<X>::max() - left) < right) [[unlikely]] {
            return std::unexpected(ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
          }
          return AstlValue{static_cast<X>(left + right)};
        } else if constexpr (std::is_same_v<X, Y> && std::is_same_v<X, std::string>) {
          return AstlValue{left + right};  // string concat
        } else {
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      addend, augend);
}

/**
 * @brief Divide the dividend by the divisor. May truncate if dividend or divisor are integral types
 * @return The quotient, or a ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE error if dividend is not arithmetic.
 */
template <typename T>
std::expected<AstlValue, astl_status_code> Divide(const AstlValue& dividend, const T divisor) {
  return std::visit(
      [=](auto&& dividend_x) -> std::expected<AstlValue, astl_status_code> {
        using X = std::decay_t<decltype(dividend_x)>;
        if constexpr (std::is_arithmetic_v<X>) {
          if (divisor == 0) {
            return std::unexpected(ASTL_STATUS_DIVIDE_BY_ZERO);
          }
          return AstlValue{static_cast<X>(dividend_x / divisor)};
        } else {
          return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
        }
      },
      dividend);
}

/**
 * @brief convert the AstlValue to a string suitable for Log debugging
 */
std::string to_string(const AstlValue& value) {
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
      value);
}

}  // namespace astl

/**
 * @brief define a formatter so std::format("{}", value) or std::format("{:X}", value)
 *        will work with AstlValue types
 */
template <>
struct std::formatter<astl::AstlValue> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {  // cppcheck-suppress unusedFunction
    // Save full format specifier for forwarding
    auto it = ctx.begin();
    while (it != ctx.end() && *it != '}') {
      ++it;
    }
    format_spec = std::string(ctx.begin(), it);
    return it;
  }

  template <typename FormatContext>
  auto format(const astl::AstlValue& value, FormatContext& ctx) const {
    return std::visit(
        [&](const auto& visited_value) -> decltype(auto) {
          using T = std::decay_t<decltype(visited_value)>;

          if constexpr (std::is_same_v<T, bool>) {
            return std::format_to(ctx.out(), "{}", visited_value ? "true" : "false");
          } else {
            // forward the format spec to the actual type
            return std::vformat_to(ctx.out(), std::string("{:" + format_spec + "}"),
                                   std::make_format_args(visited_value));
          }
        },
        value);
  }

 private:
  std::string format_spec;
};

#endif  // ASTL_VALUE_HPP_