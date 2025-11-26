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
#include "metric/formula_builder.hpp"
#include "metric/scaling_formula.hpp"

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
// ScalingFormula Tests
// =============================================================================

TEST_CASE("ScalingFormula - Basic Operations", "[ScalingFormula]") {
  constexpr double     scale_factor = 2.0;
  astl::ScalingFormula formula{scale_factor};

  SECTION("Description") {
    auto desc = formula.Description();
    REQUIRE(desc.find("SCALING") != std::string::npos);
    REQUIRE(desc.find("2") != std::string::npos);
  }

  SECTION("Apply to uint8_t") {
    astl::AstlValue input{uint8_t{10}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<uint8_t>(result->value));
    REQUIRE(std::get<uint8_t>(result->value) == 20);
  }

  SECTION("Apply to uint64_t") {
    astl::AstlValue input{uint64_t{10000}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<uint64_t>(result->value));
    REQUIRE(std::get<uint64_t>(result->value) == 20000);
  }

  SECTION("Apply to double") {
    astl::AstlValue input{3.5};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<double>(result->value));
    REQUIRE_THAT(std::get<double>(result->value), WithinAbs(7.0, 0.0001));
  }
}

TEST_CASE("ScalingFormula - Fractional Scaling", "[ScalingFormula]") {
  constexpr double     scale_factor = 0.001;
  astl::ScalingFormula formula{scale_factor};

  SECTION("Scale down uint32_t") {
    astl::AstlValue input{uint32_t{1000}};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<uint32_t>(result->value));
    REQUIRE(std::get<uint32_t>(result->value) == 1);  // 1000 * 0.001 = 1
  }

  SECTION("Scale down double") {
    astl::AstlValue input{1000.0};
    auto            result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<double>(result->value));
    REQUIRE_THAT(std::get<double>(result->value), WithinAbs(1.0, 0.0001));
  }
}

TEST_CASE("ScalingFormula - Edge Cases", "[ScalingFormula]") {
  SECTION("Scale by 1.0 (identity)") {
    astl::ScalingFormula formula{1.0};
    astl::AstlValue      input{uint32_t{42}};
    auto                 result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 42);
  }

  SECTION("Scale by 0.0") {
    astl::ScalingFormula formula{0.0};
    astl::AstlValue      input{uint32_t{12345}};
    auto                 result = formula.Apply(input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 0);
  }
}

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

TEST_CASE("FormulaBuilder - BITMASK", "[FormulaBuilder]") {
  SECTION("Hex format") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"},
        {"value",          0xFF     }
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::BitMaskFormula>(*result));

    auto&           formula = std::get<astl::BitMaskFormula>(*result);
    astl::AstlValue input{uint16_t{0x12FF}};
    auto            applied = formula.Apply(input);
    REQUIRE(applied.has_value());
    REQUIRE(std::get<uint16_t>(applied->value) == 0xFF);
  }

  SECTION("Decimal format") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"},
        {"value",          255      }
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::BitMaskFormula>(*result));

    auto&           formula = std::get<astl::BitMaskFormula>(*result);
    astl::AstlValue input{uint8_t{0xFF}};
    auto            applied = formula.Apply(input);
    REQUIRE(applied.has_value());
    REQUIRE(std::get<uint8_t>(applied->value) == 0xFF);
  }

  SECTION("Large mask value") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"        },
        {"value",          0xFFFFFFFFFFFFULL}
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::BitMaskFormula>(*result));

    auto&           formula = std::get<astl::BitMaskFormula>(*result);
    astl::AstlValue input{uint16_t{0x12FF}};
    auto            applied = formula.Apply(input);
    REQUIRE(applied.has_value());
    REQUIRE(std::get<uint16_t>(applied->value) == 0x12FF);
  }

  SECTION("Reject string value") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"},
        {"value",          "0xFF"   }
    });

    auto result = astl::BuildFormula(json);
    // Value must be a number, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

TEST_CASE("FormulaBuilder - SCALING", "[FormulaBuilder]") {
  SECTION("Integer scale factor") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "SCALING"},
        {"value",          2        }
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::ScalingFormula>(*result));

    auto&           formula = std::get<astl::ScalingFormula>(*result);
    astl::AstlValue input{uint32_t{100}};
    auto            applied = formula.Apply(input);
    REQUIRE(applied.has_value());
    REQUIRE(std::get<uint32_t>(applied->value) == 200);
  }

  SECTION("Floating point scale factor") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "SCALING"},
        {"value",          0.001    }
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::ScalingFormula>(*result));

    auto&           formula = std::get<astl::ScalingFormula>(*result);
    astl::AstlValue input{uint32_t{1000}};
    auto            applied = formula.Apply(input);
    REQUIRE(applied.has_value());
    REQUIRE(std::get<uint32_t>(applied->value) == 1);
  }

  SECTION("Case insensitive transformation name") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "ScAlInG"},
        {"value",          1.5      }
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::ScalingFormula>(*result));
  }

  SECTION("Reject string value") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "SCALING"},
        {"value",          "1.5"    }
    });

    auto result = astl::BuildFormula(json);
    // Value must be a number, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

TEST_CASE("FormulaBuilder - Null/Empty", "[FormulaBuilder]") {
  SECTION("Null JSON") {
    nlohmann::json json   = nullptr;
    auto           result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::IdentityFormula>(*result));
  }

  SECTION("Empty array") {
    nlohmann::json json   = nlohmann::json::array();
    auto           result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::IdentityFormula>(*result));
  }
}

TEST_CASE("FormulaBuilder - Invalid Input", "[FormulaBuilder]") {
  SECTION("Not an array") {
    nlohmann::json json = {
        {"transformation", "BITMASK"},
        {"value",          "0xFF"   }
    };
    auto result = astl::BuildFormula(json);
    // Must be an array, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Array element not an object") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back("invalid");
    auto result = astl::BuildFormula(json);
    // Array element must be object, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Missing transformation field") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"value", "0xFF"}
    });
    auto result = astl::BuildFormula(json);
    // Missing required field, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Missing value field") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"}
    });
    auto result = astl::BuildFormula(json);
    // Missing required field, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Unknown transformation") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "UNKNOWN"},
        {"value",          "0xFF"   }
    });
    auto result = astl::BuildFormula(json);
    // Unknown transformation, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Reject string value") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"},
        {"value",          "0xFF"   }
    });
    auto result = astl::BuildFormula(json);
    // Value must be a number, returns error
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Reject negative value") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"},
        {"value",          -1       }
    });
    auto result = astl::BuildFormula(json);
    // Negative values will be cast to large uint64_t, which is valid
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<astl::BitMaskFormula>(*result));
  }
}

TEST_CASE("FormulaBuilder - Multiple Transformations Warning", "[FormulaBuilder]") {
  SECTION("Uses first transformation only") {
    nlohmann::json json = nlohmann::json::array();
    json.push_back({
        {"transformation", "BITMASK"},
        {"value",          0xFF     }
    });
    json.push_back({
        {"transformation", "SCALING"},
        {"value",          2.0      }
    });

    auto result = astl::BuildFormula(json);
    REQUIRE(result.has_value());
    // Should use first transformation (BITMASK)
    REQUIRE(std::holds_alternative<astl::BitMaskFormula>(*result));
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

  SECTION("Apply ScalingFormula via variant") {
    astl::AnyFormula formula = astl::ScalingFormula{2.0};
    astl::AstlValue  input{uint32_t{100}};
    auto             result = astl::ApplyFormula(formula, input);
    REQUIRE(result.has_value());
    REQUIRE(std::get<uint32_t>(result->value) == 200);
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

  SECTION("ScalingFormula description") {
    astl::AnyFormula formula = astl::ScalingFormula{0.001};
    auto             desc    = astl::GetFormulaDescription(formula);
    REQUIRE(desc.find("SCALING") != std::string::npos);
    REQUIRE(desc.find("0.001") != std::string::npos);
  }

  SECTION("IdentityFormula description") {
    astl::AnyFormula formula = astl::IdentityFormula{};
    REQUIRE(astl::GetFormulaDescription(formula) == "NONE");
  }
}
