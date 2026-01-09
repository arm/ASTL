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

#ifndef BIT_MASK_FORMULA_HPP_
#define BIT_MASK_FORMULA_HPP_

#include <cstdint>
#include <expected>
#include <sstream>
#include <string>
#include <type_traits>

#include "astl/astl_errors.h"
#include "common/astl_value.hpp"
#include "metric/formula.hpp"

namespace astl {

/**
 * @brief Formula that applies a bitwise AND mask to integer values.
 *
 * This formula is useful for extracting specific bits from raw hardware registers.
 * Only supports integer types (uint8_t, uint16_t, uint32_t, uint64_t).
 */
class BitMaskFormula {
 public:
  explicit BitMaskFormula(uint64_t mask) : _mask(mask) {
    // Pre-compute description string
    std::ostringstream oss;
    oss << "BIT_MASK 0x" << std::hex << _mask;
    _description = oss.str();
  }

  [[nodiscard]] auto Apply(const AstlValue& value) const -> std::expected<AstlValue, astl_status_code> {
    return std::visit(
        [this](auto&& val) -> std::expected<AstlValue, astl_status_code> {
          using T = std::decay_t<decltype(val)>;

          // Only support integer types (not bool)
          if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            // Apply mask and return the same type
            T masked_value = static_cast<T>(val & _mask);
            return AstlValue{masked_value};
          } else {
            return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
          }
        },
        value.value);
  }

  [[nodiscard]] auto Description() const -> std::string_view { return _description; }

  [[nodiscard]] auto GetMask() const -> uint64_t { return _mask; }

 private:
  uint64_t    _mask{0};
  std::string _description;
};

static_assert(Formula<BitMaskFormula>, "BitMaskFormula does not satisfy Formula concept");

}  // namespace astl

#endif  // BIT_MASK_FORMULA_HPP_
