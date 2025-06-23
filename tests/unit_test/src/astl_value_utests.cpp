#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <format>
#include <optional>

#include "common/astl_value.hpp"

TEST_CASE("AstlValue as u8", "[AstlValue]") {
  astl::AstlValue val     = uint8_t{0x42};
  astl::AstlValue max_val = uint8_t{0xFF};
  astl::AstlValue one     = uint8_t{0x01};

  SECTION("std::max works", "[AstlValue]") {
    REQUIRE(std::max(one, val) == val);
    REQUIRE(std::max(std::optional<astl::AstlValue>{one}, std::optional<astl::AstlValue>{}) == one);
  }

  SECTION("std::min works", "[AstlValue]") {
    REQUIRE(std::min(one, val) == one);
    // NB! a null opt is < anything with a value
    const auto minimal_u8 = astl::ToVariant(astl::AstlValueMin{}, ASTL_VALUE_UINT8).value();
    REQUIRE(std::min(std::optional<astl::AstlValue>{minimal_u8}, std::optional<astl::AstlValue>{}) == std::nullopt);
  }

  SECTION("ToVariant works", "[AstlValue]") {
    const uint8_t      test_value = 0xa5;
    const astl_value_t c_value{.ui8 = test_value};
    auto               var = astl::ToVariant(c_value, ASTL_VALUE_UINT8).value();
    REQUIRE(std::get<uint8_t>(var) == test_value);

    REQUIRE(astl::ToVariant(c_value, ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
    // cppcheck-suppress integerOverflow
    const auto truly_unknown_type = static_cast<astl_value_type_t>(ASTL_VALUE_UNKNOWN - 1);
    REQUIRE(astl::ToVariant(c_value, truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("To astl_value_t", "[AstlValue]") {
    auto [astl_value, astl_type] = astl::ToAstlUnionValue(val);
    REQUIRE(astl_type == ASTL_VALUE_UINT8);
    REQUIRE(astl_value.ui8 == 0x42);
  }

  SECTION("Add works", "[AstlValue]") {
    auto sum = astl::Add(one, one);
    REQUIRE(std::get<uint8_t>(sum.value()) == 2);
  }

  SECTION("Add returns error on overflow", "[AstlValue]") {
    auto overflow = astl::Add(one, max_val);
    REQUIRE(overflow.error() == ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
  }

  SECTION("Add forbids sum of different value types u16 and u8", "[AstlValue]") {
    // Note, we could use std::common_type_t to find a integral type to hold the sum of these,
    // but with the C API associating a value_type with a metric instead of a sample, it's best not to auto-promote,
    // at least for now.
    auto incompatible = astl::Add(one, uint16_t{0x16});
    REQUIRE(incompatible.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Divide by one is identity", "[AstlValue]") { REQUIRE(astl::Divide(val, uint8_t{1}).value() == val); }

  SECTION("Divide by zero is an error", "[AstlValue]") {
    REQUIRE(astl::Divide(val, uint8_t{0}).error() == ASTL_STATUS_DIVIDE_BY_ZERO);
  }

  SECTION("Compute an average", "[AstlValue]") {
    const astl::AstlValue val_a    = uint8_t{12};
    const astl::AstlValue val_b    = uint8_t{3};
    const astl::AstlValue expected = uint8_t{7};
    const auto            sum      = astl::Add(val_a, val_b).value();
    const auto            avg      = astl::Divide(sum, 2).value();

    REQUIRE(avg == expected);
    REQUIRE(std::get<uint8_t>(avg) == 7);
  }

  SECTION("Format as hex") {
    const std::string formatted = std::format("0x{:X}", val);
    REQUIRE(formatted == "0x42");
  }
  SECTION("Format as decimal") {
    val                         = uint8_t{12};
    const std::string formatted = std::format("{}", val);
    REQUIRE(formatted == "12");
  }
}

TEST_CASE("AstlValue as ui64", "[AstlValue]") {
  astl::AstlValue val     = uint64_t{0x42};
  astl::AstlValue max_val = std::numeric_limits<uint64_t>::max();
  astl::AstlValue one     = uint64_t{0x01};

  SECTION("std::max works", "[AstlValue]") {
    REQUIRE(std::max(one, val) == val);
    REQUIRE(std::max(std::optional<astl::AstlValue>{one}, std::optional<astl::AstlValue>{}) == one);
  }

  SECTION("std::min works", "[AstlValue]") {
    REQUIRE(std::min(one, val) == one);
    // NB! a null opt is < anything with a value
    const auto minimal_u8 = astl::ToVariant(astl::AstlValueMin{}, ASTL_VALUE_UINT64).value();
    REQUIRE(std::min(std::optional<astl::AstlValue>{minimal_u8}, std::optional<astl::AstlValue>{}) == std::nullopt);
  }

  SECTION("ToVariant works", "[AstlValue]") {
    const uint64_t     test_value = 0xa5;
    const astl_value_t c_value{.ui64 = test_value};
    auto               var = astl::ToVariant(c_value, ASTL_VALUE_UINT64).value();
    REQUIRE(std::get<uint64_t>(var) == test_value);

    REQUIRE(astl::ToVariant(c_value, ASTL_VALUE_UNKNOWN).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
    // cppcheck-suppress integerOverflow
    const auto truly_unknown_type = static_cast<astl_value_type_t>(ASTL_VALUE_UNKNOWN - 1);
    REQUIRE(astl::ToVariant(c_value, truly_unknown_type).error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("To astl_value_t", "[AstlValue]") {
    auto [astl_value, astl_type] = astl::ToAstlUnionValue(val);
    REQUIRE(astl_type == ASTL_VALUE_UINT64);
    REQUIRE(astl_value.ui64 == 0x42);
  }

  SECTION("Add works", "[AstlValue]") {
    auto sum = astl::Add(one, one);
    REQUIRE(std::get<uint64_t>(sum.value()) == 2);
  }

  SECTION("Add returns error on overflow", "[AstlValue]") {
    auto overflow = astl::Add(one, max_val);
    REQUIRE(overflow.error() == ASTL_STATUS_METRIC_OVERFLOW_DETECTED);
  }

  SECTION("Add forbids sum of different value types u16 and u8", "[AstlValue]") {
    // Note, we could use std::common_type_t to find a integral type to hold the sum of these,
    // but with the C API associating a value_type with a metric instead of a sample, it's best not to auto-promote,
    // at least for now.
    auto incompatible = astl::Add(one, uint16_t{0x16});
    REQUIRE(incompatible.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Divide by one is identity", "[AstlValue]") { REQUIRE(astl::Divide(val, uint64_t{1}).value() == val); }

  SECTION("Divide by zero is an error", "[AstlValue]") {
    REQUIRE(astl::Divide(val, uint64_t{0}).error() == ASTL_STATUS_DIVIDE_BY_ZERO);
  }

  SECTION("Compute an average", "[AstlValue]") {
    const astl::AstlValue val_a    = uint64_t{12};
    const astl::AstlValue val_b    = uint64_t{3};
    const astl::AstlValue expected = uint64_t{7};
    const auto            sum      = astl::Add(val_a, val_b).value();
    const auto            avg      = astl::Divide(sum, 2).value();

    REQUIRE(avg == expected);
    REQUIRE(std::get<uint64_t>(avg) == 7);
  }

  SECTION("Format as hex") {
    const std::string formatted = std::format("0x{:04X}", val);
    REQUIRE(formatted == "0x0042");
  }
  SECTION("Format as decimal") {
    val                         = uint64_t{12};
    const std::string formatted = std::format("{}", val);
    REQUIRE(formatted == "12");
  }
}

TEST_CASE("AstlValue as string", "[AstlValue]") {
  astl::AstlValue hello = std::string{"Hello, "};
  astl::AstlValue world = std::string{"world!"};

  SECTION("To astl_value_t", "[AstlValue]") {
    auto [astl_value, astl_type] = astl::ToAstlUnionValue(hello);
    REQUIRE(astl_type == ASTL_VALUE_STRING);
    REQUIRE(std::strncmp(astl_value.str, "Hello, ", std::strlen("Hello, ")) == 0);
  }

  SECTION("Add works for string concat", "[AstlValue]") {
    auto concat = astl::Add(hello, world);
    REQUIRE(std::get<std::string>(concat.value()) == "Hello, world!");
  }

  SECTION("Add forbids sum of different value types str and integrals", "[AstlValue]") {
    auto incompatible = astl::Add(hello, uint16_t{0x16});
    REQUIRE(incompatible.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  SECTION("Format is basically identity function for strings") {
    std::string formatted = std::format("{}", hello);
    REQUIRE(formatted == "Hello, ");
  }
}