// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "../../test_includes.hpp"  // must come first before any Catch2 usage
#include "common/key_value_text_utils.hpp"
#include "common/system_info.hpp"
#include "common/text_parse_utils.hpp"

TEST_CASE("FindFirstKeyValue returns the first matching parsed key", "[system_info]") {
  constexpr std::string_view cpuinfo = R"(processor       : 0
model name      : Some CPU Model
flags           : fp asimd sve
)";

  const auto values = astl::text::ParseKeyValueText(cpuinfo);

  REQUIRE(astl::text::FindFirstKeyValue(values, {"Hardware", "model name"}) == "Some CPU Model");
  REQUIRE(astl::text::FindFirstKeyValue(values, {"flags"}) == "fp asimd sve");
}

TEST_CASE("ParseLeadingUint64 accepts a numeric prefix before units", "[system_info]") {
  REQUIRE(astl::text::ParseLeadingUint64("  16384 kB") == 16384);
  REQUIRE(astl::text::ParseLeadingUint64("42") == 42);
  REQUIRE_FALSE(astl::text::ParseLeadingUint64("kB").has_value());
}

TEST_CASE("InferCpuTypeFromCpuInfoText prefers direct model fields", "[system_info]") {
  constexpr std::string_view cpuinfo = R"(processor       : 0
model name      : Some CPU Model
CPU implementer : 0x41
CPU part        : 0xd0c
)";

  REQUIRE(astl::detail::InferCpuTypeFromCpuInfoText(cpuinfo) == "Some CPU Model");
}

TEST_CASE("InferCpuTypeFromCpuInfoText synthesizes ARM cpu type from implementer and part", "[system_info]") {
  constexpr std::string_view cpuinfo = R"(processor       : 2
BogoMIPS        : 50.00
Features        : fp asimd evtstrm aes pmull sha1 sha2 crc32
CPU implementer : 0x41
CPU architecture: 8
CPU variant     : 0x3
CPU part        : 0xd0c
CPU revision    : 1
)";

  REQUIRE(astl::detail::InferCpuTypeFromCpuInfoText(cpuinfo) == "Arm part 0xd0c (arch 8, variant 0x3, revision 1)");
}

TEST_CASE("InferCpuTypeFromCpuInfoText returns empty string when cpuinfo lacks identifying fields", "[system_info]") {
  constexpr std::string_view cpuinfo = R"(processor : 0
BogoMIPS  : 50.00
Features  : fp asimd
)";

  REQUIRE(astl::detail::InferCpuTypeFromCpuInfoText(cpuinfo).empty());
}
