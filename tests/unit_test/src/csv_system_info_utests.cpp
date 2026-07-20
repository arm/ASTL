// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "../../test_includes.hpp"  // must come first before any Catch2 usage
#include "output/csv_system_info.hpp"

TEST_CASE("FormatReportFloatingValue always emits two decimal places", "[csv_system_info]") {
  REQUIRE(astl::FormatReportFloatingValue(3.14159) == "3.14");
  REQUIRE(astl::FormatReportFloatingValue(0.00001) == "0.00");
  REQUIRE(astl::FormatReportFloatingValue(-2.0101) == "-2.01");
  REQUIRE(astl::FormatReportFloatingValue(10.00009) == "10.00");
}
