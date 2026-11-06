// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/generators/catch_generators.hpp>
#include <limits>

#include "../../test_includes.hpp"
#include "scmi/scmi_clock_correlation.hpp"

namespace {
auto RawTime(int64_t nanoseconds) -> astl::ProcessedSampleTimestamp {
  return astl::ProcessedSampleTimestamp{std::chrono::nanoseconds{nanoseconds}};
}
}  // namespace

TEST_CASE("SCMI correlation halves decimal intervals with upward rounding", "[scmi_clock_correlation]") {
  using astl::scmi_detail::ClockCorrelationReadSpacing;
  CHECK(ClockCorrelationReadSpacing({1200, -6}).value().count() == 600'000);
  CHECK(ClockCorrelationReadSpacing({25, -3}).value().count() == 12'500'000);
  CHECK(ClockCorrelationReadSpacing({1, 0}).value().count() == 500'000'000);
  CHECK(ClockCorrelationReadSpacing({3, -9}).value().count() == 2);
  CHECK(ClockCorrelationReadSpacing({16, -10}).value().count() == 1);
  CHECK(ClockCorrelationReadSpacing({1, std::numeric_limits<int32_t>::min()}).value().count() == 1);
  CHECK(ClockCorrelationReadSpacing({std::numeric_limits<uint32_t>::max(), 0}).value().count() ==
        2'147'483'647'500'000'000);
  CHECK_FALSE(ClockCorrelationReadSpacing({0, -6}));
  CHECK_FALSE(ClockCorrelationReadSpacing({1, std::numeric_limits<int32_t>::max()}));
  CHECK_FALSE(ClockCorrelationReadSpacing({std::numeric_limits<uint32_t>::max(), 1}));
}

TEST_CASE("SCMI correlation chooses signed minimum and includes duplicate observations", "[scmi_clock_correlation]") {
  astl::scmi_detail::ClockCorrelationMinimum minimum{1'000'000};
  CHECK_FALSE(minimum.Correlation());
  REQUIRE(minimum.ConsiderSample(1000, RawTime(500)) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Correlation()->raw_monotonic_at_start == RawTime(500));
  REQUIRE(minimum.ConsiderSample(2000, RawTime(1499)) == ASTL_STATUS_SUCCESS);
  REQUIRE(minimum.ConsiderSample(2000, RawTime(1599)) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Correlation()->raw_monotonic_at_start == RawTime(1499));
  // An artificial earlier raw observation proves duplicates participate in selection.
  REQUIRE(minimum.ConsiderSample(2000, RawTime(1400)) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Correlation()->raw_monotonic_at_start == RawTime(1400));
  CHECK(minimum.Correlation()->native_at_start == 2000);
  CHECK(minimum.Count() == 2);
  CHECK(minimum.ConsiderSample(1999, RawTime(1600)) == ASTL_STATUS_BAD_CONFIGURATION);
  CHECK(minimum.ConsiderSample(0, RawTime(1600)) == ASTL_STATUS_BAD_CONFIGURATION);
  astl::scmi_detail::ClockCorrelationMinimum invalid{0};
  CHECK(invalid.ConsiderSample(1, RawTime(100)) == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("SCMI correlation keeps earliest ties and compares fractional offsets exactly", "[scmi_clock_correlation]") {
  astl::scmi_detail::ClockCorrelationMinimum minimum{3};
  REQUIRE(minimum.ConsiderSample(1, RawTime(1'000'000)) == ASTL_STATUS_SUCCESS);
  REQUIRE(minimum.ConsiderSample(4, RawTime(2'000'000)) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Correlation()->native_at_start == 1);
  astl::scmi_detail::ClockCorrelationMinimum fractional{3};
  REQUIRE(fractional.ConsiderSample(1, RawTime(1'000'000)) == ASTL_STATUS_SUCCESS);
  REQUIRE(fractional.ConsiderSample(2, RawTime(1'333'333)) == ASTL_STATUS_SUCCESS);
  CHECK(fractional.Correlation()->native_at_start == 2);
  CHECK(fractional.Correlation()->ticks == astl::NativeToMonotonicRawRatio{1'000'000, 3});
}

TEST_CASE("SCMI correlation exact comparison covers full timestamp and rate widths", "[scmi_clock_correlation]") {
  const auto rate =
      GENERATE(astl::kilohertz{3}, astl::kilohertz{1'000'000}, std::numeric_limits<astl::kilohertz>::max());
  astl::scmi_detail::ClockCorrelationMinimum minimum{rate};
  const auto                                 native = std::numeric_limits<uint64_t>::max() - 1;
  REQUIRE(minimum.ConsiderSample(native, RawTime(std::numeric_limits<int64_t>::max())) == ASTL_STATUS_SUCCESS);
  REQUIRE(minimum.ConsiderSample(native + 1, RawTime(std::numeric_limits<int64_t>::min())) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Correlation()->native_at_start == native + 1);
  CHECK(minimum.Correlation()->raw_monotonic_at_start == RawTime(std::numeric_limits<int64_t>::min()));
}

TEST_CASE("SCMI minimum comparison covers full-width elapsed differences", "[scmi_clock_correlation]") {
  const auto lowest         = std::numeric_limits<int64_t>::min();
  const auto highest        = std::numeric_limits<int64_t>::max();
  const auto native_highest = std::numeric_limits<uint64_t>::max();
  SECTION("a larger full-width raw delta keeps the first pair") {
    astl::scmi_detail::ClockCorrelationMinimum minimum{1'000'000};
    REQUIRE(minimum.ConsiderSample(1, RawTime(lowest)) == ASTL_STATUS_SUCCESS);
    REQUIRE(minimum.ConsiderSample(native_highest, RawTime(highest)) == ASTL_STATUS_SUCCESS);
    CHECK(minimum.Correlation()->native_at_start == 1);
  }
  SECTION("a smaller full-width raw delta selects the later pair") {
    astl::scmi_detail::ClockCorrelationMinimum minimum{1'000'000};
    REQUIRE(minimum.ConsiderSample(1, RawTime(lowest + 2)) == ASTL_STATUS_SUCCESS);
    REQUIRE(minimum.ConsiderSample(native_highest, RawTime(highest)) == ASTL_STATUS_SUCCESS);
    CHECK(minimum.Correlation()->native_at_start == native_highest);
  }
  SECTION("maximum-rate remainder products preserve fractional ordering") {
    constexpr auto                             rate = std::numeric_limits<astl::kilohertz>::max();
    astl::scmi_detail::ClockCorrelationMinimum minimum{rate};
    REQUIRE(minimum.ConsiderSample(100, RawTime(-100)) == ASTL_STATUS_SUCCESS);
    REQUIRE(minimum.ConsiderSample(100 + static_cast<uint64_t>(rate) - 1, RawTime(999899)) == ASTL_STATUS_SUCCESS);
    CHECK(minimum.Correlation()->raw_monotonic_at_start == RawTime(999899));
  }
}

TEST_CASE("SCMI minimum retains one pair and ignores larger offsets", "[scmi_clock_correlation]") {
  astl::scmi_detail::ClockCorrelationMinimum minimum{1'000'000};
  REQUIRE(minimum.ConsiderSample(100, RawTime(1000)) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Correlation()->native_at_start == 100);
  CHECK(minimum.Correlation()->raw_monotonic_at_start == RawTime(1000));
  REQUIRE(minimum.ConsiderSample(200, RawTime(1150)) == ASTL_STATUS_SUCCESS);
  REQUIRE(minimum.ConsiderSample(300, RawTime(1300)) == ASTL_STATUS_SUCCESS);
  CHECK(minimum.Count() == 3);
  CHECK(minimum.Correlation()->native_at_start == 100);
  CHECK(astl::NormalizeToCorrelatedRawTimestamp(400, *minimum.Correlation()) == RawTime(1300));
  // Ordering follows the last valid read, even when the best pair is older.
  CHECK(minimum.ConsiderSample(250, RawTime(1400)) == ASTL_STATUS_BAD_CONFIGURATION);
  CHECK(minimum.Count() == 3);
}

TEST_CASE("SCMI minimum mappings normalize long captures without redundant scaling", "[scmi_clock_correlation]") {
  const auto                                 rate = GENERATE(astl::kilohertz{1'000}, astl::kilohertz{1'000'000});
  astl::scmi_detail::ClockCorrelationMinimum minimum{rate};
  REQUIRE(minimum.ConsiderSample(100, RawTime(1'000'000)) == ASTL_STATUS_SUCCESS);
  REQUIRE(minimum.ConsiderSample(100 + rate, RawTime(2'000'000)) == ASTL_STATUS_SUCCESS);
  const auto correlation = minimum.Correlation();
  REQUIRE(correlation);
  CHECK(correlation->ticks.den == 1);
  constexpr uint64_t elapsed_ms = 3 * 60 * 60 * 1'000;
  const auto         native     = 100 + (elapsed_ms * rate);
  CHECK(astl::NormalizeToCorrelatedRawTimestamp(native, *correlation) == RawTime(10'800'001'000'000));
}
