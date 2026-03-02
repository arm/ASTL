// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>

#include "../../test_includes.hpp"  // include before catch2
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
    REQUIRE(astl::AstlValue::Divide(val, uint8_t{0}).error() == ASTL_STATUS_DIVIDE_BY_ZERO);
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
    REQUIRE(astl::AstlValue::Divide(val, uint64_t{0}).error() == ASTL_STATUS_DIVIDE_BY_ZERO);
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
