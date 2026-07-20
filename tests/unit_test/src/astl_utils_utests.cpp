// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "../../test_includes.hpp"
#include "astl_utils.hpp"

TEST_CASE("CompareDigitRuns compares numeric runs without integer conversion", "[astl_utils]") {
  SECTION("shorter significant run is smaller") {
    std::size_t left_index  = 5;
    std::size_t right_index = 5;

    const int result = astl::CompareDigitRuns("core-2", left_index, "core-10", right_index);

    REQUIRE(result < 0);
    REQUIRE(left_index == std::string{"core-2"}.size());
    REQUIRE(right_index == std::string{"core-10"}.size());
  }

  SECTION("longer significant run is larger") {
    std::size_t left_index  = 5;
    std::size_t right_index = 5;

    const int result = astl::CompareDigitRuns("core-100", left_index, "core-99", right_index);

    REQUIRE(result > 0);
    REQUIRE(left_index == std::string{"core-100"}.size());
    REQUIRE(right_index == std::string{"core-99"}.size());
  }

  SECTION("equal numeric values ignore leading zeros but advance past them") {
    std::size_t left_index  = 4;
    std::size_t right_index = 4;

    const int result = astl::CompareDigitRuns("tlm-0007-extra", left_index, "tlm-7-extra", right_index);

    REQUIRE(result == 0);
    REQUIRE(left_index == 8);
    REQUIRE(right_index == 5);
  }

  SECTION("equal-length significant runs compare lexicographically") {
    std::size_t left_index  = 1;
    std::size_t right_index = 1;

    const int result = astl::CompareDigitRuns("a42", left_index, "a43", right_index);

    REQUIRE(result < 0);
    REQUIRE(left_index == std::string{"a42"}.size());
    REQUIRE(right_index == std::string{"a43"}.size());
  }
}

TEST_CASE("NaturalLess orders strings with human numeric ordering", "[astl_utils]") {
  SECTION("digit runs sort by numeric magnitude") {
    REQUIRE(astl::NaturalLess("core2-frequency", "core10-frequency"));
    REQUIRE_FALSE(astl::NaturalLess("core10-frequency", "core2-frequency"));
  }

  SECTION("leading zeros do not change numeric ordering") {
    REQUIRE_FALSE(astl::NaturalLess("tlm-007", "tlm-7"));
    REQUIRE_FALSE(astl::NaturalLess("tlm-7", "tlm-007"));
  }

  SECTION("non-digit characters fall back to lexicographic ordering") {
    REQUIRE(astl::NaturalLess("core-a", "core-b"));
    REQUIRE_FALSE(astl::NaturalLess("core-b", "core-a"));
  }

  SECTION("shorter exhausted string sorts before a remaining suffix") {
    REQUIRE(astl::NaturalLess("core", "core1"));
    REQUIRE_FALSE(astl::NaturalLess("core1", "core"));
  }

  SECTION("natural comparator can be used to sort a collection") {
    std::vector<std::string> names{"core10", "core1", "core02", "core2", "core100"};

    std::ranges::sort(names, astl::NaturalLess);

    REQUIRE(std::ranges::is_sorted(names, astl::NaturalLess));
    REQUIRE(names.front() == "core1");
    REQUIRE(names.back() == "core100");
  }
}
