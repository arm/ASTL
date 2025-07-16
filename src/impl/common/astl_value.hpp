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
struct AstlValue {
  std::variant<uint8_t, uint16_t, uint32_t, uint64_t, float, double, bool, std::string> value;

  explicit AstlValue(uint8_t val);
  explicit AstlValue(uint16_t val);
  explicit AstlValue(uint32_t val);
  explicit AstlValue(uint64_t val);
  explicit AstlValue(float val);
  explicit AstlValue(double val);
  explicit AstlValue(bool val);
  explicit AstlValue(std::string val);

  auto IsArithmetic() const -> bool {
    return std::visit(
        [](auto&& arg) -> bool {
          using T = std::decay_t<decltype(arg)>;
          return std::is_arithmetic_v<T> || std::is_same_v<T, bool>;
        },
        value);
  }

  /**
   * @brief convert a C-style astl_value_t to a AstlValue according to the specified astl_value_type_t
   *
   * @return an AstlValue instance with the same value as val, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static std::expected<AstlValue, astl_status_code> FromUnion(const astl_value_t& val, astl_value_type_t type);

  /**
   * @brief Create an AstlValue of 0 value of the largest representative of the given type
   *        For example, if the type given is ASTL_VALUE_UINT16, this will return an AstlValue with
   *        uint64_t as its representation. A float or double will give back a double
   *
   *        This is useful for creating a base for arithmetic operations, like a running sum for average
   */
  static std::expected<AstlValue, astl_status_code> FromUnionPromoting(astl_value_type_t type);

  /**
   * @brief Create an AstlValue of the given type with the minimal possible value
   *
   * @return an AstlValue instance with the minimum possible value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static std::expected<AstlValue, astl_status_code> FromMinimum(astl_value_type_t type);

  /**
   * @brief Create an AstlValue of the given type as close to '0' as possible
   *
   * @return an AstlValue instance of 0 value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static std::expected<AstlValue, astl_status_code> FromZero(astl_value_type_t type);

  /**
   * @brief Create an AstlValue of the given type with the maximum possible value
   *
   * @return an AstlValue instance with the max representable value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
   * @note if type == ASTL_VALUE_STRING, this returns ASTL_STATUS_VALID_VALUE_TYPE
   */
  static std::expected<AstlValue, astl_status_code> FromMaximum(astl_value_type_t type);

  /**
   * @brief Compute the sum of two AstlValues of the same underlying type.
   *
   * @return value of common larger type of the two operands or a status code:
   *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
   *   - ASTL_STATUS_INVALID_VALUE_TYPE if operands aren't similar types
   */
  static std::expected<AstlValue, astl_status_code> Add(const AstlValue& addend, const AstlValue& augend);

  /**
   * @brief Compute the difference of two AstlValues of the same underlying type.
   *
   * @return value of common larger type of the two operands or a status code:
   *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
   *   - ASTL_STATUS_INVALID_VALUE_TYPE if operands aren't similar types
   */
  static std::expected<AstlValue, astl_status_code> Subtract(const AstlValue& minuend, const AstlValue& subtrahend);

  /**
   * @brief Divide the dividend by the divisor. May truncate if dividend or divisor are integral types
   * @return The quotient, or a ASTL_STATUS_INVALID_VALUE_TYPE error if dividend is not arithmetic.
   */
  template <typename DivisorType>
  static std::expected<AstlValue, astl_status_code> Divide(const AstlValue& dividend, const DivisorType divisor) {
    return std::visit(
        [=](auto&& dividend_x) -> std::expected<AstlValue, astl_status_code> {
          using DividendType = std::decay_t<decltype(dividend_x)>;
          if constexpr (std::is_arithmetic_v<DividendType>) {
            if (divisor == 0) {
              return std::unexpected(ASTL_STATUS_DIVIDE_BY_ZERO);
            }

            // If divisor is double or float, return result as the divisor's type
            if constexpr (std::is_same_v<DivisorType, double> || std::is_same_v<DivisorType, float>) {
              return AstlValue{static_cast<DivisorType>(static_cast<DivisorType>(dividend_x) / divisor)};
            } else {
              return AstlValue{static_cast<DividendType>(static_cast<DividendType>(dividend_x) /
                                                         static_cast<DividendType>(divisor))};
            }
          } else {
            return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
          }
        },
        dividend.value);
  }

  /**
   * @brief convert the C++ style AstlValue to a pair of C API astl_value_t and astl_value_type_t
   *
   * @return a std::pair where the first element is the value and the second idenfifies the type
   * @note if the AstlValue is of type std::string, the returned astl_value_t will have a char *
   *       pointing to the AstlValue's internal string buffer. So the AstlValue must outlive the returned
   *       astl_value_t.
   */
  std::pair<astl_value_t, astl_value_type_t> ToAstlUnionValue() const;

  auto operator<=>(AstlValue const&) const = default;
};

/**
 * @brief convert the AstlValue to a string suitable for Log debugging
 */
std::string to_string(const AstlValue& variant_value);

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
  auto format(const astl::AstlValue& variant_value, FormatContext& ctx) const {
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
        variant_value.value);
  }

 private:
  std::string format_spec;
};

#endif  // ASTL_VALUE_HPP_