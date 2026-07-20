// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>

#include "../../test_includes.hpp"  // include before catch2
#include "astl_internal_status.hpp"
#include "common/astl_value.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("AstlValue as u8", "[AstlValue]") {
  astl::AstlValue val     = astl::AstlValue{uint8_t{0x42}};
  astl::AstlValue max_val = astl::AstlValue{uint8_t{0xFF}};
  astl::AstlValue one     = astl::AstlValue{uint8_t{0x01}};

  SECTION("std::max works", "[AstlValue]") {
    REQUIRE(std::max(one, val) == val);
    REQUIRE(std::max(std::optional<astl::AstlValue>{one}, std::optional<astl::AstlValue>{}) == one);
    astl::AstlValue max_u8 = astl::AstlValue::FromMaximum(ASTL_VALUE_UINT8).value();
    REQUIRE(std::max(max_u8, one) == max_u8);
    REQUIRE(max_u8 == max_val);
  }

  SECTION("std::min works", "[AstlValue]") {
    REQUIRE(std::min(one, val) == one);
    const auto minimal_u8 = astl::AstlValue::FromMinimum(ASTL_VALUE_UINT8).value();
    REQUIRE(std::min(minimal_u8, one) == minimal_u8);
  }

  SECTION("FromUnion works", "[AstlValue]") {
    const uint8_t      test_value = 0xa5;
    const astl_value_t c_value{.ui8 = test_value};
    auto               astl_value = astl::AstlValue::FromUnion(c_value, ASTL_VALUE_UINT8).value();
    REQUIRE(std::get<uint8_t>(astl_value.value) == test_value);

    REQUIRE(astl::AstlValue::FromUnion(c_value, ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
    const auto truly_unknown_type = static_cast<astl_value_type_t>(ASTL_VALUE_UNKNOWN - 1);
    REQUIRE(astl::AstlValue::FromUnion(c_value, truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("To astl_value_t", "[AstlValue]") {
    auto [astl_value, astl_type] = val.ToAstlUnionValue();
    REQUIRE(astl_type == ASTL_VALUE_UINT8);
    REQUIRE(astl_value.ui8 == 0x42);
  }

  SECTION("Add works", "[AstlValue]") {
    auto sum = astl::AstlValue::Add(one, one);
    REQUIRE(std::get<uint8_t>(sum.value().value) == 2);
  }

  SECTION("Add returns error on overflow", "[AstlValue]") {
    auto overflow = astl::AstlValue::Add(one, max_val);
    REQUIRE(overflow.error() == ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
  }

  SECTION("Sum of different value types does some auto promotion", "[AstlValue]") {
    const auto sixteen   = astl::AstlValue{uint16_t{0x16}};
    const auto seventeen = astl::AstlValue{uint16_t{0x17}};
    auto       sum       = astl::AstlValue::Add(one, sixteen);
    REQUIRE(sum.value() == seventeen);
  }

  SECTION("Divide by one is identity", "[AstlValue]") {
    REQUIRE(astl::AstlValue::Divide(val, uint8_t{1}).value() == val);
  }

  SECTION("Divide by zero is an error", "[AstlValue]") {
    REQUIRE(astl::AstlValue::Divide(val, uint8_t{0}).error() == astl::kInternalDivideByZero);
  }

  SECTION("Compute an average", "[AstlValue]") {
    const astl::AstlValue val_a{uint8_t{12}};
    const astl::AstlValue val_b{uint8_t{3}};
    const astl::AstlValue expected{uint8_t{7}};
    const auto            sum = astl::AstlValue::Add(val_a, val_b).value();
    const auto            avg = astl::AstlValue::Divide(sum, uint8_t{2}).value();
    REQUIRE(avg == expected);
    REQUIRE(std::get<uint8_t>(avg.value) == 7);
  }

  SECTION("Format as hex") {
    const std::string formatted = std::format("0x{:X}", val);
    REQUIRE(formatted == "0x42");
  }
  SECTION("Format as decimal") {
    val                         = astl::AstlValue{uint8_t{12}};
    const std::string formatted = std::format("{}", val);
    REQUIRE(formatted == "12");
  }
}

TEST_CASE("AstlValue as ui64", "[AstlValue]") {
  auto val     = astl::AstlValue{uint64_t{0x42}};
  auto max_val = astl::AstlValue{std::numeric_limits<uint64_t>::max()};
  auto one     = astl::AstlValue{uint64_t{0x01}};

  SECTION("std::max works", "[AstlValue]") {
    REQUIRE(std::max(one, val) == val);
    REQUIRE(std::max(std::optional<astl::AstlValue>{one}, std::optional<astl::AstlValue>{}) == one);
  }

  SECTION("std::min works", "[AstlValue]") {
    REQUIRE(std::min(one, val) == one);
    const auto minimal_ui64 = astl::AstlValue::FromMinimum(ASTL_VALUE_UINT64).value();
    REQUIRE(std::min(minimal_ui64, one) == minimal_ui64);
  }

  SECTION("FromUnion works", "[AstlValue]") {
    const uint64_t     test_value = 0xa5;
    const astl_value_t c_value{.ui64 = test_value};
    auto               var = astl::AstlValue::FromUnion(c_value, ASTL_VALUE_UINT64).value();
    REQUIRE(std::get<uint64_t>(var.value) == test_value);

    REQUIRE(astl::AstlValue::FromUnion(c_value, ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
    const auto truly_unknown_type = static_cast<astl_value_type_t>(ASTL_VALUE_UNKNOWN - 1);
    REQUIRE(astl::AstlValue::FromUnion(c_value, truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("To astl_value_t", "[AstlValue]") {
    auto [astl_value, astl_type] = val.ToAstlUnionValue();
    REQUIRE(astl_type == ASTL_VALUE_UINT64);
    REQUIRE(astl_value.ui64 == 0x42);
  }

  SECTION("Add works", "[AstlValue]") {
    auto sum = astl::AstlValue::Add(one, one);
    REQUIRE(std::get<uint64_t>(sum.value().value) == 2);
  }

  SECTION("Add returns error on overflow", "[AstlValue]") {
    auto overflow = astl::AstlValue::Add(one, max_val);
    REQUIRE(overflow.error() == ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
  }

  SECTION("Permit sum of different value types u16 and u8", "[AstlValue]") {
    astl::AstlValue one_u8{uint8_t{0x01}};
    auto            promote_to_u16 = astl::AstlValue::Add(one_u8, astl::AstlValue{uint16_t{0x16}}).value();
    auto [value, type]             = promote_to_u16.ToAstlUnionValue();
    REQUIRE(value.ui16 == (1 + 0x16));
    REQUIRE(type == ASTL_VALUE_UINT16);  // ui64 because 'one' has that as its internal type
  }

  SECTION("Divide by one is identity", "[AstlValue]") {
    REQUIRE(astl::AstlValue::Divide(val, uint64_t{1}).value() == val);
  }

  SECTION("Divide by zero is an error", "[AstlValue]") {
    REQUIRE(astl::AstlValue::Divide(val, uint64_t{0}).error() == astl::kInternalDivideByZero);
  }

  SECTION("Compute an average", "[AstlValue]") {
    const astl::AstlValue val_a{uint64_t{12}};
    const astl::AstlValue val_b{uint64_t{3}};
    const astl::AstlValue expected{uint64_t{7}};
    const auto            sum = astl::AstlValue::Add(val_a, val_b).value();
    const auto            avg = astl::AstlValue::Divide(sum, 2).value();
    REQUIRE(avg == expected);
    REQUIRE(std::get<uint64_t>(avg.value) == 7);
  }

  SECTION("Format as hex") {
    const std::string formatted = std::format("0x{:04X}", val);
    REQUIRE(formatted == "0x0042");
  }
  SECTION("Format as decimal") {
    val                         = astl::AstlValue{uint64_t{12}};
    const std::string formatted = std::format("{}", val);
    REQUIRE(formatted == "12");
  }
}

TEST_CASE("AstlValue invalid constructors", "[AstlValue]") {
  const uint64_t     test_value{0xa5};
  const astl_value_t c_value{.ui64 = test_value};
  const auto         truly_unknown_type = static_cast<astl_value_type_t>(ASTL_VALUE_UNKNOWN - 1);
  REQUIRE(astl::AstlValue::FromUnion(astl_value_t{}, ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  REQUIRE(astl::AstlValue::FromUnion(c_value, truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  REQUIRE(astl::AstlValue::FromMinimum(ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  REQUIRE(astl::AstlValue::FromMinimum(truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  REQUIRE(astl::AstlValue::FromMaximum(ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  REQUIRE(astl::AstlValue::FromMaximum(truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}

TEST_CASE("AstlValue::FromUnion", "[AstlValue]") {
  // u8
  astl_value_t union_value{.ui8 = 0xa5};
  auto         astl_value = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_UINT8).value();
  REQUIRE(std::get<uint8_t>(astl_value.value) == 0xa5);
  // u16
  union_value.ui16 = 0x1234;
  astl_value       = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_UINT16).value();
  REQUIRE(std::get<uint16_t>(astl_value.value) == 0x1234);
  // u32
  union_value.ui32 = 0x1234568;
  astl_value       = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_UINT32).value();
  REQUIRE(std::get<uint32_t>(astl_value.value) == 0x1234568);
  // u64
  union_value.ui64 = 0x1234568deadaced;
  astl_value       = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_UINT64).value();
  REQUIRE(std::get<uint64_t>(astl_value.value) == 0x1234568deadaced);
  // f32
  union_value.fp32 = 3.14F;
  astl_value       = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_FLOAT32).value();
  REQUIRE_THAT(std::get<float>(astl_value.value), WithinAbs(3.14F, 0.0001));
  // f64
  union_value.fp64 = 3.14159;
  astl_value       = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_FLOAT64).value();
  REQUIRE_THAT(std::get<double>(astl_value.value), WithinAbs(3.14159, 0.000001));
  // bool
  union_value.b8 = true;
  astl_value     = astl::AstlValue::FromUnion(union_value, ASTL_VALUE_BOOL8).value();
  REQUIRE(std::get<bool>(astl_value.value) == true);
}

TEST_CASE("AstlValue::FromZero", "[AstlValue]") {
  // u8
  auto astl_value = astl::AstlValue::FromZero(ASTL_VALUE_UINT8).value();
  REQUIRE(std::get<uint8_t>(astl_value.value) == 0);
  // u16
  astl_value = astl::AstlValue::FromZero(ASTL_VALUE_UINT16).value();
  REQUIRE(std::get<uint16_t>(astl_value.value) == 0);
  // u32
  astl_value = astl::AstlValue::FromZero(ASTL_VALUE_UINT32).value();
  REQUIRE(std::get<uint32_t>(astl_value.value) == 0);
  // u64
  astl_value = astl::AstlValue::FromZero(ASTL_VALUE_UINT64).value();
  REQUIRE(std::get<uint64_t>(astl_value.value) == 0);
  // f32
  astl_value = astl::AstlValue::FromZero(ASTL_VALUE_FLOAT32).value();
  REQUIRE_THAT(std::get<float>(astl_value.value), WithinAbs(0.0F, 0.0001));
  // f64
  astl_value = astl::AstlValue::FromZero(ASTL_VALUE_FLOAT64).value();
  REQUIRE_THAT(std::get<double>(astl_value.value), WithinAbs(0.0, 0.000001));
  // bool
  astl_value = astl::AstlValue::FromZero(ASTL_VALUE_BOOL8).value();
  REQUIRE(std::get<bool>(astl_value.value) == false);
}

TEST_CASE("AstlValue::FromMinimum", "[AstlValue]") {
  auto astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_UINT8).value();
  REQUIRE(std::get<uint8_t>(astl_value.value) == 0);
  astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_UINT16).value();
  REQUIRE(std::get<uint16_t>(astl_value.value) == 0);
  astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_UINT32).value();
  REQUIRE(std::get<uint32_t>(astl_value.value) == 0);
  astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_UINT64).value();
  REQUIRE(std::get<uint64_t>(astl_value.value) == 0);
  astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_FLOAT32).value();
  // note that floating point values are signed
  REQUIRE_THAT(std::get<float>(astl_value.value), WithinAbs(std::numeric_limits<float>::lowest(), 0.0001F));
  astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_FLOAT64).value();
  REQUIRE_THAT(std::get<double>(astl_value.value), WithinAbs(std::numeric_limits<double>::lowest(), 0.000001));
  astl_value = astl::AstlValue::FromMinimum(ASTL_VALUE_BOOL8).value();
  REQUIRE(std::get<bool>(astl_value.value) == false);
}

TEST_CASE("AstlValue::FromMaximum", "[AstlValue]") {
  auto astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_UINT8).value();
  REQUIRE(std::get<uint8_t>(astl_value.value) == 0xFF);
  astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_UINT16).value();
  REQUIRE(std::get<uint16_t>(astl_value.value) == 0xFFFF);
  astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_UINT32).value();
  REQUIRE(std::get<uint32_t>(astl_value.value) == 0xFFFFFFFF);
  astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_UINT64).value();
  REQUIRE(std::get<uint64_t>(astl_value.value) == 0xFFFFFFFFFFFFFFFF);
  astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_FLOAT32).value();
  REQUIRE_THAT(std::get<float>(astl_value.value), WithinAbs(std::numeric_limits<float>::max(), 0.0001F));
  astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_FLOAT64).value();
  REQUIRE_THAT(std::get<double>(astl_value.value), WithinAbs(std::numeric_limits<double>::max(), 0.000001));
  astl_value = astl::AstlValue::FromMaximum(ASTL_VALUE_BOOL8).value();
  REQUIRE(std::get<bool>(astl_value.value) == true);
  // string is not supported
}

TEST_CASE("AstlValue::ToAstlUnionValue", "[AstlValue]") {
  astl::AstlValue val{uint8_t{0x42}};
  auto [astl_value, astl_type] = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_UINT8);
  REQUIRE(astl_value.ui8 == 0x42);

  val                             = astl::AstlValue{uint16_t{0x1234}};
  std::tie(astl_value, astl_type) = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_UINT16);
  REQUIRE(astl_value.ui16 == 0x1234);

  val                             = astl::AstlValue{uint32_t{0x12345678}};
  std::tie(astl_value, astl_type) = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_UINT32);
  REQUIRE(astl_value.ui32 == 0x12345678);

  val                             = astl::AstlValue{uint64_t{0x12345678deadaced}};
  std::tie(astl_value, astl_type) = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_UINT64);
  REQUIRE(astl_value.ui64 == 0x12345678deadaced);

  val                             = astl::AstlValue{3.14F};
  std::tie(astl_value, astl_type) = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_FLOAT32);
  REQUIRE_THAT(astl_value.fp32, WithinAbs(3.14, 0.001));

  val                             = astl::AstlValue{3.14159};
  std::tie(astl_value, astl_type) = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_FLOAT64);
  REQUIRE_THAT(astl_value.fp64, WithinAbs(3.14159, 0.00001));

  val                             = astl::AstlValue{true};
  std::tie(astl_value, astl_type) = val.ToAstlUnionValue();
  REQUIRE(astl_type == ASTL_VALUE_BOOL8);
  REQUIRE(astl_value.b8 == true);
}

TEST_CASE("AstlValue utility conversions and arithmetic edge cases", "[AstlValue]") {
  SECTION("FromUnionPromoting widens integral and boolean inputs") {
    REQUIRE(std::holds_alternative<uint64_t>(astl::AstlValue::FromUnionPromoting(ASTL_VALUE_UINT16)->value));
    REQUIRE(std::holds_alternative<double>(astl::AstlValue::FromUnionPromoting(ASTL_VALUE_FLOAT32)->value));
    REQUIRE(std::holds_alternative<uint64_t>(astl::AstlValue::FromUnionPromoting(ASTL_VALUE_BOOL8)->value));
    REQUIRE(astl::AstlValue::FromUnionPromoting(ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Conversion helpers cover numeric and bool cases") {
    std::string as_text;

    const astl::AstlValue floating_value{12.5};
    REQUIRE(floating_value.IsArithmetic());
    REQUIRE(floating_value.ToStringValue(as_text));
    REQUIRE(as_text == "12.500000");
    REQUIRE(floating_value.ToDouble().value() == 12.5);
    REQUIRE(floating_value.ToInt64().value() == 12);

    const astl::AstlValue bool_value{true};
    REQUIRE(bool_value.IsArithmetic());
    REQUIRE(bool_value.ToStringValue(as_text));
    REQUIRE(as_text == "true");
    REQUIRE(bool_value.ToDouble().error() == ASTL_STATUS_INVALID_VALUE_TYPE);
    REQUIRE(bool_value.ToInt64().error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Add supports floating promotion and rejects mixed integral floating arithmetic") {
    const auto floating_sum = astl::AstlValue::Add(astl::AstlValue{1.25F}, astl::AstlValue{2.75});
    REQUIRE(floating_sum.has_value());
    REQUIRE_THAT(std::get<double>(floating_sum->value), WithinAbs(4.0, 0.000001));

    const auto bool_sum = astl::AstlValue::Add(astl::AstlValue{true}, astl::AstlValue{true});
    REQUIRE(bool_sum.has_value());
    REQUIRE(std::get<uint8_t>(bool_sum->value) == 2);

    const auto invalid_sum = astl::AstlValue::Add(astl::AstlValue{uint8_t{7}}, astl::AstlValue{1.0F});
    REQUIRE(invalid_sum.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Subtract detects unsigned underflow and supports floating subtraction") {
    const auto underflow = astl::AstlValue::Subtract(astl::AstlValue{uint8_t{1}}, astl::AstlValue{uint8_t{2}});
    REQUIRE(underflow.error() == ASTL_STATUS_METRIC_OVERFLOW_DETECTED);

    const auto difference = astl::AstlValue::Subtract(astl::AstlValue{2.5}, astl::AstlValue{0.5F});
    REQUIRE(difference.has_value());
    REQUIRE_THAT(std::get<double>(difference->value), WithinAbs(2.0, 0.000001));
  }

  SECTION("Divide handles boolean numerators and floating divisors") {
    const auto half = astl::AstlValue::Divide(astl::AstlValue{true}, 2.0);
    REQUIRE(half.has_value());
    REQUIRE_THAT(std::get<double>(half->value), WithinAbs(0.5, 0.000001));
  }
}

TEST_CASE("AstlValue cross-variant arithmetic matrix", "[AstlValue][matrix]") {
  SECTION("Integral-family equality is numeric across all stored integral variants") {
    const std::vector<astl::AstlValue> ones{
        astl::AstlValue{uint8_t{1}},  astl::AstlValue{uint16_t{1}}, astl::AstlValue{uint32_t{1}},
        astl::AstlValue{uint64_t{1}}, astl::AstlValue{true},
    };

    for (const auto& lhs : ones) {
      for (const auto& rhs : ones) {
        REQUIRE(lhs == rhs);
      }
    }

    REQUIRE(astl::AstlValue{false} != astl::AstlValue{uint64_t{1}});
    REQUIRE(astl::AstlValue{uint32_t{2}} != astl::AstlValue{uint8_t{1}});
  }

  SECTION("Integral-floating equality rejects all cross-family comparisons") {
    const std::vector<astl::AstlValue> integrals{
        astl::AstlValue{uint8_t{3}},  astl::AstlValue{uint16_t{3}}, astl::AstlValue{uint32_t{3}},
        astl::AstlValue{uint64_t{3}}, astl::AstlValue{true},
    };
    const std::vector<astl::AstlValue> floating{
        astl::AstlValue{3.0F},
        astl::AstlValue{3.0},
    };

    for (const auto& lhs : integrals) {
      for (const auto& rhs : floating) {
        REQUIRE_FALSE(lhs == rhs);
        REQUIRE_FALSE(rhs == lhs);
      }
    }
  }

  SECTION("Ordering remains numeric within families and deterministic across families") {
    REQUIRE(astl::AstlValue{uint8_t{2}} < astl::AstlValue{uint64_t{3}});
    REQUIRE(astl::AstlValue{false} < astl::AstlValue{uint16_t{1}});
    REQUIRE(astl::AstlValue{float{2.25F}} < astl::AstlValue{double{2.5}});
    REQUIRE(astl::AstlValue{double{7.5}} > astl::AstlValue{float{7.0F}});
    REQUIRE(astl::AstlValue{uint32_t{99}} < astl::AstlValue{double{0.0}});
    REQUIRE(astl::AstlValue{float{0.0F}} > astl::AstlValue{true});
  }

  SECTION("Add promotes to the wider arithmetic representation") {
    const auto sum_u32_u8 = astl::AstlValue::Add(astl::AstlValue{uint32_t{9}}, astl::AstlValue{uint8_t{2}});
    REQUIRE(sum_u32_u8.has_value());
    REQUIRE(std::holds_alternative<uint32_t>(sum_u32_u8->value));
    REQUIRE(std::get<uint32_t>(sum_u32_u8->value) == 11);

    const auto sum_u64_bool = astl::AstlValue::Add(astl::AstlValue{uint64_t{9}}, astl::AstlValue{true});
    REQUIRE(sum_u64_bool.has_value());
    REQUIRE(std::holds_alternative<uint64_t>(sum_u64_bool->value));
    REQUIRE(std::get<uint64_t>(sum_u64_bool->value) == 10);

    const auto sum_f32_f32 = astl::AstlValue::Add(astl::AstlValue{1.5F}, astl::AstlValue{2.0F});
    REQUIRE(sum_f32_f32.has_value());
    REQUIRE(std::holds_alternative<float>(sum_f32_f32->value));
    REQUIRE_THAT(std::get<float>(sum_f32_f32->value), WithinAbs(3.5F, 0.0001F));

    const auto sum_f64_f32 = astl::AstlValue::Add(astl::AstlValue{1.5}, astl::AstlValue{2.0F});
    REQUIRE(sum_f64_f32.has_value());
    REQUIRE(std::holds_alternative<double>(sum_f64_f32->value));
    REQUIRE_THAT(std::get<double>(sum_f64_f32->value), WithinAbs(3.5, 0.000001));
  }

  SECTION("Subtract promotes and preserves expected result types") {
    const auto diff_u32_u8 = astl::AstlValue::Subtract(astl::AstlValue{uint32_t{9}}, astl::AstlValue{uint8_t{2}});
    REQUIRE(diff_u32_u8.has_value());
    REQUIRE(std::holds_alternative<uint32_t>(diff_u32_u8->value));
    REQUIRE(std::get<uint32_t>(diff_u32_u8->value) == 7);

    const auto diff_bool_bool = astl::AstlValue::Subtract(astl::AstlValue{true}, astl::AstlValue{false});
    REQUIRE(diff_bool_bool.has_value());
    REQUIRE(std::holds_alternative<bool>(diff_bool_bool->value));
    REQUIRE(std::get<bool>(diff_bool_bool->value));

    const auto diff_f64_f32 = astl::AstlValue::Subtract(astl::AstlValue{5.5}, astl::AstlValue{2.25F});
    REQUIRE(diff_f64_f32.has_value());
    REQUIRE(std::holds_alternative<double>(diff_f64_f32->value));
    REQUIRE_THAT(std::get<double>(diff_f64_f32->value), WithinAbs(3.25, 0.000001));
  }

  SECTION("Divide instantiates integral and floating divisors for multiple stored types") {
    const auto quarter = astl::AstlValue::Divide(astl::AstlValue{uint16_t{8}}, uint16_t{4});
    REQUIRE(quarter.has_value());
    REQUIRE(std::holds_alternative<uint16_t>(quarter->value));
    REQUIRE(std::get<uint16_t>(quarter->value) == 2);

    const auto float_half = astl::AstlValue::Divide(astl::AstlValue{9.0F}, 2U);
    REQUIRE(float_half.has_value());
    REQUIRE(std::holds_alternative<float>(float_half->value));
    REQUIRE_THAT(std::get<float>(float_half->value), WithinAbs(4.5F, 0.0001F));

    const auto double_half = astl::AstlValue::Divide(astl::AstlValue{9.0}, 2.0F);
    REQUIRE(double_half.has_value());
    REQUIRE(std::holds_alternative<float>(double_half->value));
    REQUIRE_THAT(std::get<float>(double_half->value), WithinAbs(4.5F, 0.0001F));

    const auto bool_div = astl::AstlValue::Divide(astl::AstlValue{true}, uint8_t{1});
    REQUIRE(bool_div.has_value());
    REQUIRE(std::holds_alternative<uint8_t>(bool_div->value));
    REQUIRE(std::get<uint8_t>(bool_div->value) == 1);
  }
}

TEST_CASE("AstlValue formatter specialization supports custom specs", "[AstlValue][format]") {
  REQUIRE(std::format("{:08X}", astl::AstlValue{uint32_t{0x2A}}) == "0000002A");
  REQUIRE(std::format("{:.2f}", astl::AstlValue{3.14159}) == "3.14");
  REQUIRE(std::format("{}", astl::AstlValue{false}) == "false");
}

TEST_CASE("astl::to_string(AstlValue)", "[AstlValue]") {
  astl::AstlValue val{uint8_t{0x42}};
  REQUIRE(astl::to_string(val) == "66");  // use format with specifiers if you want to keep hex format
  val = astl::AstlValue{uint64_t{0x42}};
  REQUIRE(astl::to_string(val) == "66");

  val = astl::AstlValue{true};
  REQUIRE(astl::to_string(val) == "true");
  val = astl::AstlValue{false};
  REQUIRE(astl::to_string(val) == "false");

  val = astl::AstlValue{3.14};
  REQUIRE(astl::to_string(val) == "3.14");
}

TEST_CASE("AstlValue mixed-variant equality and ordering", "[AstlValue][comparison]") {
  SECTION("Equality across different integral variants is numeric") {
    REQUIRE(astl::AstlValue{uint8_t{42}} == astl::AstlValue{uint16_t{42}});
    REQUIRE(astl::AstlValue{bool{true}} == astl::AstlValue{uint8_t{1}});
    REQUIRE(astl::AstlValue{bool{false}} == astl::AstlValue{uint8_t{0}});
  }

  SECTION("Equality across float and double variants is numeric") {
    REQUIRE(astl::AstlValue{float{3.5F}} == astl::AstlValue{double{3.5}});
  }

  SECTION("Integral-vs-floating equality is always false") {
    REQUIRE_FALSE(astl::AstlValue{uint8_t{7}} == astl::AstlValue{float{7.0F}});
    REQUIRE_FALSE(astl::AstlValue{uint64_t{1}} == astl::AstlValue{double{1.0}});
  }

  SECTION("Ordering across integral variants is numeric") {
    REQUIRE(astl::AstlValue{uint8_t{4}} < astl::AstlValue{uint16_t{5}});
    REQUIRE(astl::AstlValue{uint64_t{9}} > astl::AstlValue{uint8_t{8}});
  }

  SECTION("Ordering across floating variants is numeric") {
    REQUIRE(astl::AstlValue{float{1.25F}} < astl::AstlValue{double{1.5}});
    REQUIRE(astl::AstlValue{double{2.0}} > astl::AstlValue{float{1.99F}});
  }

  SECTION("Mixed integral/floating ordering is deterministic") {
    REQUIRE(astl::AstlValue{uint8_t{5}} < astl::AstlValue{float{5.0F}});
    REQUIRE(astl::AstlValue{double{0.0}} > astl::AstlValue{uint64_t{999}});
    REQUIRE(astl::AstlValue{uint64_t{999}} < astl::AstlValue{double{0.0}});
  }
}
