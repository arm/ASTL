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

#include <cstdint>
#include <expected>
#include <format>
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
  std::variant<uint8_t, uint16_t, uint32_t, uint64_t, float, double, bool> value;

  explicit AstlValue(uint8_t val);
  explicit AstlValue(uint16_t val);
  explicit AstlValue(uint32_t val);
  explicit AstlValue(uint64_t val);
  explicit AstlValue(float val);
  explicit AstlValue(double val);
  explicit AstlValue(bool val);

  auto IsArithmetic() const -> bool {
    return std::visit(
        [](auto&& arg) -> bool {
          using T = std::decay_t<decltype(arg)>;
          return std::is_arithmetic_v<T> || std::is_same_v<T, bool>;
        },
        value);
  }

  /**
   * @brief Convert the AstlValue to a string representation
   *
   * @param[out] result_str The string to store the result in
   * @return true if conversion was successful, false otherwise
   */
  auto ToStringValue(std::string& result_str) const -> bool {
    return std::visit(
        [&result_str](auto&& arg) -> bool {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, bool>) {
            result_str = arg ? "true" : "false";
            return true;
          } else if constexpr (std::is_arithmetic_v<T>) {
            result_str = std::to_string(arg);
            return true;
          }
          return false;
        },
        value);
  }

  /**
   * @brief Convert the AstlValue to a double if it holds an arithmetic type
   *
   * @return std::expected containing the double value if successful, or ASTL_STATUS_INVALID_VALUE_TYPE if not
   * arithmetic
   */
  auto ToDouble() const -> std::expected<double, astl_status_code> {
    return std::visit(
        [](auto&& arg) -> std::expected<double, astl_status_code> {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, uint8_t>) {
            return static_cast<double>(arg);
          } else if constexpr (std::is_same_v<T, uint16_t>) {
            return static_cast<double>(arg);
          } else if constexpr (std::is_same_v<T, uint32_t>) {
            return static_cast<double>(arg);
          } else if constexpr (std::is_same_v<T, uint64_t>) {
            return static_cast<double>(arg);
          } else if constexpr (std::is_same_v<T, float>) {
            return static_cast<double>(arg);
          } else if constexpr (std::is_same_v<T, double>) {
            return arg;
          } else {
            // type - cannot convert to double
            return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
          }
        },
        value);
  }

  /**
   * @brief Convert the AstlValue to an int64 if it holds an arithmetic type
   *
   * @return std::expected containing the int64 value if successful, or ASTL_STATUS_INVALID_VALUE_TYPE if not
   * arithmetic
   */
  auto ToInt64() const -> std::expected<int64_t, astl_status_code> {
    return std::visit(
        [](auto&& arg) -> std::expected<int64_t, astl_status_code> {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, uint8_t>) {
            return static_cast<int64_t>(arg);
          } else if constexpr (std::is_same_v<T, uint16_t>) {
            return static_cast<int64_t>(arg);
          } else if constexpr (std::is_same_v<T, uint32_t>) {
            return static_cast<int64_t>(arg);
          } else if constexpr (std::is_same_v<T, uint64_t>) {
            return static_cast<int64_t>(arg);
          } else if constexpr (std::is_same_v<T, float>) {
            return static_cast<int64_t>(arg);
          } else if constexpr (std::is_same_v<T, double>) {
            return static_cast<int64_t>(arg);
          } else {
            // type - cannot convert to int64
            return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
          }
        },
        value);
  }

  /**
   * @brief convert a C-style astl_value_t to a AstlValue according to the specified astl_value_type_t
   *
   * @return an AstlValue instance with the same value as val, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static auto FromUnion(const astl_value_t& val, astl_value_type_t type) -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Create an AstlValue of 0 value of the largest representative of the given type
   *        For example, if the type given is ASTL_VALUE_UINT16, this will return an AstlValue with
   *        uint64_t as its representation. A float or double will give back a double
   *
   *        This is useful for creating a base for arithmetic operations, like a running sum for average
   */
  static auto FromUnionPromoting(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Create an AstlValue of the given type with the minimal possible value
   *
   * @return an AstlValue instance with the minimum possible value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static auto FromMinimum(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Create an AstlValue of the given type as close to '0' as possible
   *
   * @return an AstlValue instance of 0 value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static auto FromZero(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Create an AstlValue of the given type with the maximum possible value
   *
   * @return an AstlValue instance with the max representable value of `type`, or a ASTL_STATUS_INVALID_VALUE_TYPE
   */
  static auto FromMaximum(astl_value_type_t type) -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Compute the sum of two AstlValues of the same underlying type.
   *
   * @return value of common larger type of the two operands or a status code:
   *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
   *   - ASTL_STATUS_INVALID_VALUE_TYPE if operands aren't similar types
   */
  static auto Add(const AstlValue& addend, const AstlValue& augend) -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Compute the difference of two AstlValues of the same underlying type.
   *
   * @return value of common larger type of the two operands or a status code:
   *   - ASTL_STATUS_METRIC_OVERFLOW_DETECTED if this would overflow their representations
   *   - ASTL_STATUS_INVALID_VALUE_TYPE if operands aren't similar types
   */
  static auto Subtract(const AstlValue& minuend, const AstlValue& subtrahend)
      -> std::expected<AstlValue, astl_status_code>;

  /**
   * @brief Divide the dividend by the divisor. May truncate if dividend or divisor are integral types
   * @return The quotient, or a ASTL_STATUS_INVALID_VALUE_TYPE error if dividend is not arithmetic.
   */
  template <typename DivisorType>
  static auto Divide(const AstlValue& dividend, const DivisorType divisor)
      -> std::expected<AstlValue, astl_status_code> {
    return std::visit(
        [=](auto&& dividend_x) -> std::expected<AstlValue, astl_status_code> {
          // Dividenttype is the simplified type of divident, but promoted from bool to uint8_t if needed
          using DividendType = std::conditional_t<std::is_same_v<std::decay_t<decltype(dividend_x)>, bool>, uint8_t,
                                                  std::decay_t<decltype(dividend_x)>>;
          if constexpr (std::is_arithmetic_v<DividendType>) {
            if (divisor == 0) {
              return std::unexpected(ASTL_STATUS_DIVIDE_BY_ZERO);
            }

            auto promote_if_bool = [](auto value) {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<T, bool>) {
                return static_cast<uint8_t>(value ? 1 : 0);
              } else {
                return value;
              }
            };

            if constexpr (std::is_same_v<DivisorType, double> || std::is_same_v<DivisorType, float>) {
              auto lhs    = static_cast<DivisorType>(promote_if_bool(dividend_x));
              auto result = static_cast<DivisorType>(lhs / static_cast<DivisorType>(divisor));
              return AstlValue{result};
            } else {
              auto lhs    = static_cast<DividendType>(promote_if_bool(dividend_x));
              auto result = static_cast<DividendType>(lhs / static_cast<DividendType>(divisor));
              return AstlValue{result};
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
   */
  auto ToAstlUnionValue() const -> std::pair<astl_value_t, astl_value_type_t>;

  auto operator<=>(AstlValue const&) const = default;
};

/**
 * @brief convert the AstlValue to a string suitable for Log debugging
 */
auto to_string(const AstlValue& variant_value) -> std::string;

}  // namespace astl

/**
 * @brief define a formatter so std::format("{}", value) or std::format("{:X}", value)
 *        will work with AstlValue types
 */
template <>
struct std::formatter<astl::AstlValue> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    // Note: parse() is required by the std::formatter contract even if some translation units
    // don't instantiate format patterns that exercise it; retaining it (rather than suppressing)
    // avoids ad-hoc cppcheck suppression.
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
