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

#include <cstdint>
#include <nlohmann/json.hpp>

#include "../../test_includes.hpp"  // include before catch2
#include "common/astl_value.hpp"
#include "metric/bit_mask_formula.hpp"
#include "metric/expression_formula.hpp"
#include "metric/formula_builder.hpp"

using Catch::Matchers::WithinAbs;

// =============================================================================
// BitMaskFormula Tests
// =============================================================================

TEST_CASE("BitMaskFormula - Basic Operations", "[BitMaskFormula]") {
  constexpr uint64_t   mask = 0xFF;
  astl::BitMaskFormula formula{mask};

  SECTION("Description") { REQUIRE(formula.Description() == "BIT_MASK 0xff"); }

  SECTION("Apply to uint8_t") {
    astl::AstlValue input{uint8_t{0xAB}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<uint8_t>(result->value));
    REQUIRE(std::get<uint8_t>(result->value) == 0xAB);
  }

  SECTION("Apply to uint64_t") {
    astl::AstlValue input{uint64_t{0xFFFFFFFFFFFFFFFF}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<uint64_t>(result->value));
    REQUIRE(std::get<uint64_t>(result->value) == 0xFF);
  }

  SECTION("Reject non-integer types") {
    // Should fail for double
    astl::AstlValue input_double{3.14};
    auto            result_double = formula.Apply(input_double);
    REQUIRE_FALSE(result_double.has_value());
    REQUIRE(result_double.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

TEST_CASE("BitMaskFormula - Edge Cases", "[BitMaskFormula]") {
  SECTION("All bits set mask") {
    astl::BitMaskFormula formula_all_bits{0xFFFFFFFFFFFFFFFF};
    astl::AstlValue      input{uint64_t{0x123456789ABCDEF0}};
    auto                 result = formula_all_bits.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x123456789ABCDEF0);
  }

  SECTION("Zero mask") {
    astl::BitMaskFormula formula_zero{0x0};
    astl::AstlValue      input{uint32_t{0xFFFFFFFF}};
    auto                 result = formula_zero.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 0);
  }

  SECTION("Single bit mask") {
    astl::BitMaskFormula formula_single{0x1};
    astl::AstlValue      input{uint8_t{0xFF}};
    auto                 result = formula_single.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint8_t>(result->value) == 0x1);
  }

  SECTION("High bit mask") {
    astl::BitMaskFormula formula_high{0x8000000000000000};
    astl::AstlValue      input{uint64_t{0xFFFFFFFFFFFFFFFF}};
    auto                 result = formula_high.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x8000000000000000);
  }
}

// =============================================================================
// ScalingFormula Tests - REMOVED: ScalingFormula replaced by ExpressionFormula
// Use ExpressionFormula with expressions like "value * 2.0" for scaling
// =============================================================================

// =============================================================================
// IdentityFormula Tests
// =============================================================================

TEST_CASE("IdentityFormula - Pass-through", "[IdentityFormula]") {
  astl::IdentityFormula formula;

  SECTION("Description") { REQUIRE(formula.Description() == "NONE"); }

  SECTION("Pass through uint8_t") {
    astl::AstlValue input{uint8_t{42}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<uint8_t>(result->value));
    REQUIRE(std::get<uint8_t>(result->value) == 42);
  }

  SECTION("Pass through double") {
    astl::AstlValue input{3.14159};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<double>(result->value));
    REQUIRE_THAT(std::get<double>(result->value), WithinAbs(3.14159, 0.00001));
  }
}

// =============================================================================
// FormulaBuilder Tests
// =============================================================================

TEST_CASE("FormulaBuilder - Null/Empty", "[FormulaBuilder]") {
  SECTION("Null JSON") {
    nlohmann::json json   = nullptr;
    auto           result = astl::BuildFormula(std::optional<nlohmann::json>{json});
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::IdentityFormula>(*result));
  }

  SECTION("Empty string") {
    nlohmann::json json   = "";
    auto           result = astl::BuildFormula(std::optional<nlohmann::json>{json});
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::IdentityFormula>(*result));
  }
}

TEST_CASE("FormulaBuilder - Invalid Input", "[FormulaBuilder]") {
  SECTION("Non-string, non-null type") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back("invalid");
    auto result = astl::BuildFormula(std::optional<nlohmann::json>{json});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Object type") {
    nlohmann::json json = {
        {"transformation", "BITMASK"},
        {"value",          "0xFF"   }
    };
    auto result = astl::BuildFormula(std::optional<nlohmann::json>{json});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Invalid expression syntax") {
    nlohmann::json json   = "value +* 2";
    auto           result = astl::BuildFormula(std::optional<nlohmann::json>{json});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Undefined variable") {
    nlohmann::json json   = "y * 2";  // Should use 'x', not 'y'
    auto           result = astl::BuildFormula(std::optional<nlohmann::json>{json});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

// =============================================================================
// AnyFormula and Helper Functions Tests
// =============================================================================

TEST_CASE("AnyFormula - ApplyFormula Helper", "[AnyFormula]") {
  SECTION("Apply BitMaskFormula via variant") {
    astl::AnyFormula formula = astl::BitMaskFormula{0xFF};
    astl::AstlValue  input{uint16_t{0x12FF}};
    auto             result = astl::ApplyFormula(formula, input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint16_t>(result->value) == 0xFF);
  }

  SECTION("Apply IdentityFormula via variant") {
    astl::AnyFormula formula = astl::IdentityFormula{};
    astl::AstlValue  input{uint8_t{42}};
    auto             result = astl::ApplyFormula(formula, input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint8_t>(result->value) == 42);
  }
}

TEST_CASE("AnyFormula - GetFormulaDescription Helper", "[AnyFormula]") {
  SECTION("BitMaskFormula description") {
    astl::AnyFormula formula = astl::BitMaskFormula{0xDEADBEEF};
    REQUIRE(astl::GetFormulaDescription(formula) == "BIT_MASK 0xdeadbeef");
  }

  SECTION("IdentityFormula description") {
    astl::AnyFormula formula = astl::IdentityFormula{};
    REQUIRE(astl::GetFormulaDescription(formula) == "NONE");
  }
}

// =============================================================================
// ExpressionFormula Tests
// =============================================================================

TEST_CASE("ExpressionFormula - Basic Arithmetic", "[ExpressionFormula]") {
  SECTION("Simple multiplication") {
    auto formula_result = astl::ExpressionFormula::Create("value * 2");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{10}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 20);
  }

  SECTION("Simple addition") {
    auto formula_result = astl::ExpressionFormula::Create("value + 100");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint32_t{50}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 150);
  }

  SECTION("Scaling factor") {
    auto formula_result = astl::ExpressionFormula::Create("value * 0.001");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{1000.0};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE_THAT(std::get<double>(result->value), WithinAbs(1.0, 0.0001));
  }
}

TEST_CASE("ExpressionFormula - Bitwise Operations", "[ExpressionFormula]") {
  SECTION("Bit shift right using >> (native operator)") {
    auto formula_result = astl::ExpressionFormula::Create("value >> 8");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x1234}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x12);
  }

  SECTION("Bit AND using bitand() function") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xABCD}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xCD);
  }

  SECTION("Combined: shift right then mask (native >> + bitand function)") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value >> 8, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) ==
            0x56);  // bitand(0x12345678 >> 8, 0xFF) = bitand(0x123456, 0xFF) = 0x56
  }

  SECTION("Bit OR using bitor() function") {
    auto formula_result = astl::ExpressionFormula::Create("bitor(value, 0xF0)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint8_t{0x0F}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint8_t>(result->value) == 0xFF);
  }

  SECTION("Bit XOR using bitxor() function") {
    auto formula_result = astl::ExpressionFormula::Create("bitxor(value, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint8_t{0xAA}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint8_t>(result->value) == 0x55);  // 0xAA XOR 0xFF = 0x55
  }

  SECTION("Bit shift left using <<") {
    auto formula_result = astl::ExpressionFormula::Create("value << 4");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x120);
  }
}

TEST_CASE("ExpressionFormula - Complex Expressions", "[ExpressionFormula]") {
  SECTION("Parentheses and operator precedence") {
    auto formula_result = astl::ExpressionFormula::Create("(value + 10) * 2");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint32_t{5}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 30);  // (5 + 10) * 2 = 30
  }

  SECTION("Multiple operations combining shift and bitwise") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value >> 8, 0xFF) * 0.001");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x64000}};  // 0x640 = 1600 in decimal
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    // bitand(0x64000 >> 8, 0xFF) = bitand(0x640, 0xFF) = 0x40 = 64
    // 64 * 0.001 = 0.064, rounded to 0 for integer
    REQUIRE(std::get<uint64_t>(result->value) == 0);
  }

  SECTION("Complex bitwise expression with parentheses") {
    auto formula_result = astl::ExpressionFormula::Create("(bitand(value >> 4, 0xFF) - 50) * 0.5");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x640}};  // 0x64 after >>4
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    // bitand(0x640 >> 4, 0xFF) = bitand(0x64, 0xFF) = 0x64 = 100
    // (100 - 50) * 0.5 = 50 * 0.5 = 25
    REQUIRE(std::get<uint64_t>(result->value) == 25);
  }
}

TEST_CASE("ExpressionFormula - Error Handling", "[ExpressionFormula]") {
  SECTION("Empty expression") {
    auto result = astl::ExpressionFormula::Create("");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Invalid expression syntax") {
    auto result = astl::ExpressionFormula::Create("value +* 2");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Undefined variable") {
    auto result = astl::ExpressionFormula::Create("y * 2");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Non-arithmetic type") {
    auto formula_result = astl::ExpressionFormula::Create("value * 2");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{true};  // bool is not supported
    auto            result = formula.Apply(input);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

TEST_CASE("ExpressionFormula - Move Semantics", "[ExpressionFormula]") {
  SECTION("Move constructor") {
    auto formula_result = astl::ExpressionFormula::Create("value * 2");
    REQUIRE(formula_result.has_value());
    auto formula1 = std::move(formula_result.value());
    auto formula2 = std::move(formula1);

    astl::AstlValue input{uint32_t{5}};
    auto            result = formula2.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 10);
  }
}

TEST_CASE("BuildFormula - String Expression Support", "[BuildFormula]") {
  SECTION("Parse string expression with native operators and bitwise function") {
    // Note: >> is native operator, but & is logical AND in tinyexpr++
    // Use bitand() function for bitwise AND
    nlohmann::json formula_json = "bitand(value >> 8, 0xFF)";
    auto           result       = astl::BuildFormula(std::optional<nlohmann::json>{formula_json});
    REQUIRE(result.has_value());

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            applied = astl::ApplyFormula(result.value(), input);
    REQUIRE(applied.has_value());
    REQUIRE(std::get<uint64_t>(applied->value) == 0x56);
  }

  SECTION("Empty string returns IdentityFormula") {
    nlohmann::json formula_json = "";
    auto           result       = astl::BuildFormula(std::optional<nlohmann::json>{formula_json});
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::IdentityFormula>(result.value()));
  }

  SECTION("Invalid string expression") {
    nlohmann::json formula_json = "invalid syntax !!!";
    auto           result       = astl::BuildFormula(std::optional<nlohmann::json>{formula_json});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("String expression in variant") {
    nlohmann::json formula_json = "value * 0.001";
    auto           result       = astl::BuildFormula(std::optional<nlohmann::json>{formula_json});
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::ExpressionFormula>(result.value()));

    auto desc = astl::GetFormulaDescription(result.value());
    REQUIRE(desc == "value * 0.001");
  }
}
