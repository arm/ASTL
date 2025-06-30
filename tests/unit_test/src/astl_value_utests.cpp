#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <format>
#include <optional>

#include "common/astl_value.hpp"

TEST_CASE("AstlValue as u8", "[AstlValue]") {
  astl::AstlValue val     = astl::AstlValue{uint8_t{0x42}};
  astl::AstlValue max_val = astl::AstlValue{uint8_t{0xFF}};
  astl::AstlValue one     = astl::AstlValue{uint8_t{0x01}};

  SECTION("std::max works", "[AstlValue]") {
    REQUIRE(std::max(one, val) == val);
    REQUIRE(std::max(std::optional<astl::AstlValue>{one}, std::optional<astl::AstlValue>{}) == one);
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
    // cppcheck-suppress integerOverflow
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
    const auto            avg = astl::AstlValue::Divide(sum, 2).value();

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
    // cppcheck-suppress integerOverflow
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

TEST_CASE("AstlValue as string", "[AstlValue]") {
  auto hello = astl::AstlValue{std::string{"Hello, "}};
  auto world = astl::AstlValue{std::string{"world!"}};

  SECTION("To astl_value_t", "[AstlValue]") {
    auto [astl_value, astl_type] = hello.ToAstlUnionValue();
    REQUIRE(astl_type == ASTL_VALUE_STRING);
    REQUIRE(std::strncmp(astl_value.str, "Hello, ", std::strlen("Hello, ")) == 0);
  }

  SECTION("Add works for string concat", "[AstlValue]") {
    auto concat = astl::AstlValue::Add(hello, world);
    REQUIRE(std::get<std::string>(concat.value().value) == "Hello, world!");
  }

  SECTION("Add forbids sum of different value types str and integrals", "[AstlValue]") {
    auto incompatible = astl::AstlValue::Add(hello, astl::AstlValue{uint16_t{0x16}});
    REQUIRE(incompatible.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Format is basically identity function for strings") {
    std::string formatted = std::format("{}", hello);
    REQUIRE(formatted == "Hello, ");
  }
}