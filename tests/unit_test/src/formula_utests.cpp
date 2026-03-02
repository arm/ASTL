// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <nlohmann/json.hpp>

#include "../../test_includes.hpp"  // include before catch2
#include "common/astl_value.hpp"
#include "metric/expression_formula.hpp"
#include "metric/formula_builder.hpp"

using Catch::Matchers::WithinAbs;

// =============================================================================
// BuildFormula Tests
// =============================================================================
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
  SECTION("Apply ExpressionFormula via variant") {
    auto expr_result = astl::ExpressionFormula::Create("bitand(value, 0xFF)");
    REQUIRE(expr_result.has_value());
    astl::AnyFormula formula = std::move(expr_result.value());
    astl::AstlValue  input{uint64_t{0x12FF}};
    auto             result = astl::ApplyFormula(formula, input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xFF);
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
  SECTION("ExpressionFormula description") {
    auto expr_result = astl::ExpressionFormula::Create("bitand(value, 0xDEADBEEF)");
    REQUIRE(expr_result.has_value());
    astl::AnyFormula formula = std::move(expr_result.value());
    REQUIRE(astl::GetFormulaDescription(formula) == "bitand(value, 0xDEADBEEF)");
  }

  SECTION("IdentityFormula description") {
    astl::AnyFormula formula = astl::IdentityFormula{};
    REQUIRE(astl::GetFormulaDescription(formula) == "NONE");
  }
}

// =============================================================================
// ExpressionFormula Bit Manipulation Tests
// =============================================================================

TEST_CASE("ExpressionFormula - Bitwise AND (bitand)", "[ExpressionFormula][BitManipulation]") {
  SECTION("Simple mask - extract low byte") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x78);
  }

  SECTION("All bits set mask") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value, 0xFFFFFFFFFFFFFFFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x123456789ABCDEF0}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x123456789ABCDEF0);
  }

  SECTION("Zero mask") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value, 0x0)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xFFFFFFFFFFFFFFFF}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0);
  }

  SECTION("Single bit mask") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value, 0x1)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xFF}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x1);
  }

  SECTION("High bit mask") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value, 0x8000000000000000)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xFFFFFFFFFFFFFFFF}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x8000000000000000);
  }
}

TEST_CASE("ExpressionFormula - Bitwise OR (bitor)", "[ExpressionFormula][BitManipulation]") {
  SECTION("Set low bits") {
    auto formula_result = astl::ExpressionFormula::Create("bitor(value, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12340000}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x123400FF);
  }

  SECTION("OR with zero") {
    auto formula_result = astl::ExpressionFormula::Create("bitor(value, 0x0)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x12345678);
  }
}

TEST_CASE("ExpressionFormula - Bitwise XOR (bitxor)", "[ExpressionFormula][BitManipulation]") {
  SECTION("Toggle bits") {
    auto formula_result = astl::ExpressionFormula::Create("bitxor(value, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x00}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xFF);
  }

  SECTION("XOR with same value gives zero") {
    auto formula_result = astl::ExpressionFormula::Create("bitxor(value, 0x12345678)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0);
  }
}

TEST_CASE("ExpressionFormula - Bitwise NOT (bitnot)", "[ExpressionFormula][BitManipulation]") {
  SECTION("Invert all bits") {
    auto formula_result = astl::ExpressionFormula::Create("bitnot(value)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x0}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xFFFFFFFFFFFFFFFF);
  }

  SECTION("Invert with mask") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(bitnot(value), 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xF0}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x0F);
  }
}

TEST_CASE("ExpressionFormula - Bit Shift Right (>>)", "[ExpressionFormula][BitManipulation]") {
  SECTION("Shift right by 8") {
    auto formula_result = astl::ExpressionFormula::Create("value >> 8");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x123456);
  }

  SECTION("Shift right by 0") {
    auto formula_result = astl::ExpressionFormula::Create("value >> 0");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x12345678);
  }

  SECTION("Extract byte 1 (bits 8-15)") {
    auto formula_result = astl::ExpressionFormula::Create("bitand(value >> 8, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x56);
  }
}

TEST_CASE("ExpressionFormula - Bit Shift Left (<<)", "[ExpressionFormula][BitManipulation]") {
  SECTION("Shift left by 8") {
    auto formula_result = astl::ExpressionFormula::Create("value << 8");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x1200);
  }

  SECTION("Shift left by 0") {
    auto formula_result = astl::ExpressionFormula::Create("value << 0");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x12345678);
  }
}

TEST_CASE("ExpressionFormula - Combined Bit Operations", "[ExpressionFormula][BitManipulation]") {
  SECTION("Extract and shift nibble") {
    // Extract bits 4-7 (second nibble)
    auto formula_result = astl::ExpressionFormula::Create("bitand(value >> 4, 0xF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xABCD}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xC);  // 0xABCD >> 4 = 0xABC, & 0xF = 0xC
  }

  SECTION("Combine shift and OR") {
    auto formula_result = astl::ExpressionFormula::Create("bitor(value << 4, 0xF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0xA}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xAF);  // 0xA << 4 = 0xA0, | 0xF = 0xAF
  }

  SECTION("Complex: extract middle byte") {
    // Extract byte 2 from 0x12345678 -> should be 0x34
    auto formula_result = astl::ExpressionFormula::Create("bitand(value >> 16, 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x34);
  }

  SECTION("Swap nibbles in a byte") {
    // Swap nibbles: (value << 4) | (value >> 4) masked to byte
    auto formula_result = astl::ExpressionFormula::Create("bitand(bitor(value << 4, value >> 4), 0xFF)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x21);  // 0x12 -> 0x21
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
    auto formula_result = astl::ExpressionFormula::Create("value / 1000");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{1000}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 1);
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
    auto formula_result = astl::ExpressionFormula::Create("bitand(value >> 8, 0xFF) * 2");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x64000}};  // 0x640 = 1600 in decimal
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    // bitand(0x64000 >> 8, 0xFF) = bitand(0x640, 0xFF) = 0x40 = 64
    // 64 * 2 = 128
    REQUIRE(std::get<uint64_t>(result->value) == 128);
  }

  SECTION("Complex bitwise expression with parentheses") {
    auto formula_result = astl::ExpressionFormula::Create("(bitand(value >> 4, 0xFF) - 50) / 2");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x640}};  // 0x64 after >>4
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    // bitand(0x640 >> 4, 0xFF) = bitand(0x64, 0xFF) = 0x64 = 100
    // (100 - 50) / 2 = 50 / 2 = 25
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
    nlohmann::json formula_json = "value * 1000";
    auto           result       = astl::BuildFormula(std::optional<nlohmann::json>{formula_json});
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::ExpressionFormula>(result.value()));

    auto desc = astl::GetFormulaDescription(result.value());
    REQUIRE(desc == "value * 1000");
  }
}

TEST_CASE("ExpressionFormula - Bitwise operators instead of functions", "[ExpressionFormula][BitManipulation]") {
  SECTION("& operator (bitwise AND)") {
    auto formula_result = astl::ExpressionFormula::Create("value & 0xFF");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input1{uint64_t{0x1234}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0x34);

    astl::AstlValue input2{uint64_t{0xDEADBEEF}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0xEF);

    astl::AstlValue input3{uint64_t{0}};
    auto            result3 = formula.Apply(input3);
    REQUIRE(result3.has_value());
    REQUIRE(std::get<uint64_t>(result3->value) == 0);
  }

  SECTION("| operator (bitwise OR)") {
    auto formula_result = astl::ExpressionFormula::Create("value | 0xFF");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input1{uint64_t{0x100}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0x1FF);

    astl::AstlValue input2{uint64_t{0}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0xFF);
  }

  SECTION("^ operator (bitwise XOR)") {
    auto formula_result = astl::ExpressionFormula::Create("value ^ 0xFFFF");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input1{uint64_t{0xFFFF}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0);

    astl::AstlValue input2{uint64_t{0x1234}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0xEDCB);
  }

  SECTION("Combined operators") {
    auto formula_result = astl::ExpressionFormula::Create("(value & 0xFF00) | 0x42");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input1{uint64_t{0x1234}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0x1242);

    astl::AstlValue input2{uint64_t{0xABCD5678}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0x5642);
  }

  SECTION("Shift and bitwise operators") {
    auto formula_result = astl::ExpressionFormula::Create("(value >> 8) & 0xFF");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    astl::AstlValue input{uint64_t{0x12345678}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0x56);
  }

  SECTION("Complex expression with operators") {
    auto formula_result = astl::ExpressionFormula::Create("((value >> 4) & 0xF) | ((value & 0xF) << 4)");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    // Swap nibbles: 0xAB -> 0xBA
    astl::AstlValue input{uint64_t{0xAB}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint64_t>(result->value) == 0xBA);
  }

  SECTION("Mixed bit patterns - alternating bits with mask") {
    // Test with patterns that include both zeros and ones
    auto formula_result = astl::ExpressionFormula::Create("value & 0xFF000000000000FF");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    // 0xAAAAAAAAAAAAAAAA = 10101010... (alternating bits)
    // Mask = 0xFF000000000000FF (first and last byte)
    // Result = 0xAA000000000000AA
    astl::AstlValue input1{uint64_t{0xAAAAAAAAAAAAAAAA}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0xAA000000000000AA);

    // 0x5555555555555555 = 01010101... (inverse alternating bits)
    // Result = 0x5500000000000055
    astl::AstlValue input2{uint64_t{0x5555555555555555}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0x5500000000000055);

    // All ones
    astl::AstlValue input3{uint64_t{0xFFFFFFFFFFFFFFFF}};
    auto            result3 = formula.Apply(input3);
    REQUIRE(result3.has_value());
    REQUIRE(std::get<uint64_t>(result3->value) == 0xFF000000000000FF);
  }

  SECTION("Mixed bit patterns - OR with alternating pattern") {
    auto formula_result = astl::ExpressionFormula::Create("value | 0xF0F0F0F0F0F0F0F0");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    // 0x0F0F0F0F0F0F0F0F OR 0xF0F0F0F0F0F0F0F0 = 0xFFFFFFFFFFFFFFFF
    astl::AstlValue input1{uint64_t{0x0F0F0F0F0F0F0F0F}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0xFFFFFFFFFFFFFFFF);

    // 0x1234567890ABCDEF OR 0xF0F0F0F0F0F0F0F0
    astl::AstlValue input2{uint64_t{0x1234567890ABCDEF}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0xF2F4F6F8F0FBFDFF);
  }

  SECTION("Mixed bit patterns - XOR with pattern") {
    auto formula_result = astl::ExpressionFormula::Create("value ^ 0xAAAAAAAAAAAAAAAA");
    REQUIRE(formula_result.has_value());
    auto& formula = formula_result.value();

    // XOR with alternating pattern flips bits selectively
    // 0xAAAAAAAAAAAAAAAA XOR 0xAAAAAAAAAAAAAAAA = 0
    astl::AstlValue input1{uint64_t{0xAAAAAAAAAAAAAAAA}};
    auto            result1 = formula.Apply(input1);
    REQUIRE(result1.has_value());
    REQUIRE(std::get<uint64_t>(result1->value) == 0);

    // 0x5555555555555555 XOR 0xAAAAAAAAAAAAAAAA = 0xFFFFFFFFFFFFFFFF
    astl::AstlValue input2{uint64_t{0x5555555555555555}};
    auto            result2 = formula.Apply(input2);
    REQUIRE(result2.has_value());
    REQUIRE(std::get<uint64_t>(result2->value) == 0xFFFFFFFFFFFFFFFF);

    // Flips specific bits
    astl::AstlValue input3{uint64_t{0xFF00FF00FF00FF00}};
    auto            result3 = formula.Apply(input3);
    REQUIRE(result3.has_value());
    REQUIRE(std::get<uint64_t>(result3->value) == 0x55AA55AA55AA55AA);
  }
}
