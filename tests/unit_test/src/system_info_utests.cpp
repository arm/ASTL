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

TEST_CASE("Wall clock conversion supports offsets and UTC calendar boundaries", "[wallclock]") {
  const astl::WallClockCorrelation reference{1000, 0, 1};
  REQUIRE(astl::CorrelateWallClock(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{1001}}, reference) == 1);
  REQUIRE(astl::CorrelateWallClock(astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{999}}, reference) == -1);
  REQUIRE(astl::FormatWallClockUtc(0) == "1970-01-01T00:00:00.000000000Z");
  REQUIRE(astl::FormatWallClockUtc(-1) == "1969-12-31T23:59:59.999999999Z");
  REQUIRE(astl::FormatWallClockUtc(951782400123456789LL) == "2000-02-29T00:00:00.123456789Z");
}

TEST_CASE("Wall clock conversion rejects arithmetic overflow", "[wallclock]") {
  constexpr auto min       = std::numeric_limits<int64_t>::min();
  constexpr auto max       = std::numeric_limits<int64_t>::max();
  const auto     timestamp = [](int64_t raw) { return astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{raw}}; };
  REQUIRE_FALSE(astl::CorrelateWallClock(timestamp(min), {1, 0, 0}));
  REQUIRE_FALSE(astl::CorrelateWallClock(timestamp(max), {-1, 0, 0}));
  REQUIRE_FALSE(astl::CorrelateWallClock(timestamp(1), {0, max, 0}));
  REQUIRE_FALSE(astl::CorrelateWallClock(timestamp(-1), {0, min, 0}));
}

TEST_CASE("Wall clock snapshot brackets the realtime clock", "[wallclock]") {
  const auto before =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto reference = astl::CaptureWallClockCorrelation();
  const auto after =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  REQUIRE(reference);
  REQUIRE(reference->realtime_ns >= before);
  REQUIRE(reference->realtime_ns <= after);
  REQUIRE(reference->uncertainty_ns >= 0);
}

TEST_CASE("UTC formatting covers the entire signed nanosecond range", "[wallclock]") {
  REQUIRE(astl::FormatWallClockUtc(std::numeric_limits<int64_t>::min()) == "1677-09-21T00:12:43.145224192Z");
  REQUIRE(astl::FormatWallClockUtc(std::numeric_limits<int64_t>::max()) == "2262-04-11T23:47:16.854775807Z");
}

TEST_CASE("Clock read bracketing bounds odd intervals and rejects invalid intervals", "[wallclock]") {
  REQUIRE(astl::detail::MakeWallClockCorrelation(10, 100, 13) == astl::WallClockCorrelation{11, 100, 2});
  REQUIRE(astl::detail::MakeWallClockCorrelation(10, 100, 10) == astl::WallClockCorrelation{10, 100, 0});
  REQUIRE_FALSE(astl::detail::MakeWallClockCorrelation(13, 100, 10));
  REQUIRE_FALSE(astl::detail::MakeWallClockCorrelation(INT64_MIN, 100, INT64_MAX));
}

#if defined(__linux__) && defined(__LP64__)
TEST_CASE("Clock capture rejects realtime values outside signed nanosecond range", "[wallclock]") {
  REQUIRE(astl::detail::TimespecToNanoseconds({-9223372037LL, 145224192}) == INT64_MIN);
  REQUIRE(astl::detail::TimespecToNanoseconds({9223372036LL, 854775807}) == INT64_MAX);
  REQUIRE(astl::detail::TimespecToNanoseconds({-1, 999999999}) == -1);
  REQUIRE_FALSE(astl::detail::TimespecToNanoseconds({-9223372037LL, 145224191}));
  REQUIRE_FALSE(astl::detail::TimespecToNanoseconds({9223372036LL, 854775808}));
  REQUIRE_FALSE(astl::detail::TimespecToNanoseconds({INT64_MAX, 0}));
  REQUIRE_FALSE(astl::detail::TimespecToNanoseconds({0, -1}));
  REQUIRE_FALSE(astl::detail::TimespecToNanoseconds({0, 1000000000}));
}
#endif
