// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include "astl/astl_errors.h"
#include "astl_cxx_compat.hpp"
#include "astl_json.hpp"

namespace compat = astl::compat;

// Verify aliases follow library feature availability; the C++20 preset exercises the fallbacks.
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
static_assert(std::is_same_v<compat::expected<int, int>, std::expected<int, int>>);
#else
static_assert(std::is_same_v<compat::expected<int, int>, tl::expected<int, int>>);
#endif
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L
static_assert(std::is_same_v<compat::optional<int>, std::optional<int>>);
#else
static_assert(std::is_same_v<compat::optional<int>, tl::optional<int>>);
#endif

TEST_CASE("Compatibility expected preserves status errors and void success", "[cxx_compat]") {
  compat::expected<void, astl_status_code> result;
  REQUIRE(result.has_value());
  result = compat::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  result = {};
  REQUIRE(result.has_value());

  compat::expected<int, astl_status_code> failed = compat::unexpected{ASTL_STATUS_BAD_ARGUMENT};
  REQUIRE_THROWS_AS(failed.value(), compat::bad_expected_access<astl_status_code>);
}

TEST_CASE("Compatibility expected supports move-only values and in-place spans", "[cxx_compat]") {
  auto make_value = []() -> compat::expected<std::unique_ptr<int>, astl_status_code> {
    return std::make_unique<int>(42);
  };
  auto original = make_value();
  auto moved    = std::move(original);
  REQUIRE(**moved == 42);
  REQUIRE(original.value() == nullptr);

  std::array                                               values{1, 2};
  compat::expected<std::span<const int>, astl_status_code> view(compat::in_place, values);
  REQUIRE(view->size() == 2);
  REQUIRE((*view)[1] == 2);
}

TEST_CASE("Compatibility optional transforms values and propagates empty results", "[cxx_compat]") {
  const compat::optional<std::string> value{"cpu"};
  auto                                length = value.transform([](const std::string& text) { return text.size(); });
  REQUIRE(length.value() == 3);

  bool                          called = false;
  compat::optional<std::string> empty{compat::nullopt};
  auto                          missing = empty.transform([&](const std::string&) {
    called = true;
    return std::string{"unexpected"};
  });
  REQUIRE_FALSE(called);
  REQUIRE(missing.value_or("fallback") == "fallback");
  REQUIRE_THROWS_AS(empty.value(), compat::bad_optional_access);
}

TEST_CASE("Compatibility optional transforms move-only temporaries", "[cxx_compat]") {
  auto moved = compat::optional<std::unique_ptr<int>>{std::make_unique<int>(7)}.transform(
      [](std::unique_ptr<int> value) { return value; });
  REQUIRE(**moved == 7);
}

TEST_CASE("Compatibility optional preserves JSON values and nulls", "[cxx_compat]") {
  const nlohmann::json present = "cpu";
  auto                 value   = present.get<compat::optional<std::string>>();
  REQUIRE(value.value() == "cpu");
  REQUIRE(nlohmann::json(value) == present);

  const nlohmann::json empty = nullptr;
  empty.get_to(value);
  REQUIRE_FALSE(value.has_value());
  REQUIRE(nlohmann::json(value).is_null());
  REQUIRE_THROWS_AS(nlohmann::json(42).get_to(value), nlohmann::json::type_error);
}
