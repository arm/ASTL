// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#ifndef ASTL_WALL_CLOCK_HPP_
#define ASTL_WALL_CLOCK_HPP_

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

namespace astl {

/** Convert without overflowing even for invalid or extreme timestamps supplied by an archive. */
inline auto CorrelateWallClockNanoseconds(int64_t raw, int64_t reference_raw, int64_t reference_realtime)
    -> std::optional<int64_t> {
  constexpr auto min = std::numeric_limits<int64_t>::min();
  constexpr auto max = std::numeric_limits<int64_t>::max();
  if ((reference_raw > 0 && raw < min + reference_raw) || (reference_raw < 0 && raw > max + reference_raw)) {
    return std::nullopt;
  }
  const auto delta = raw - reference_raw;
  if ((delta > 0 && reference_realtime > max - delta) || (delta < 0 && reference_realtime < min - delta)) {
    return std::nullopt;
  }
  return reference_realtime + delta;
}

/** ISO 8601 UTC, independent of the analysis host's timezone, with nanosecond precision. */
inline auto FormatWallClockUtc(int64_t realtime_ns) -> std::string {
  using std::chrono::days;
  using std::chrono::floor;
  using std::chrono::hh_mm_ss;
  using std::chrono::nanoseconds;
  using std::chrono::sys_time;
  using std::chrono::year_month_day;
  constexpr int               fractional_second_digits = 9;
  const sys_time<nanoseconds> time{nanoseconds{realtime_ns}};
  const auto                  day = floor<days>(time);
  const year_month_day        date{day};
  constexpr int64_t           nanos_per_day = 86'400'000'000'000;
  auto                        within_day    = realtime_ns % nanos_per_day;
  if (within_day < 0) {
    within_day += nanos_per_day;
  }
  const hh_mm_ss     time_of_day{nanoseconds{within_day}};
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-' << std::setw(2)
         << static_cast<unsigned>(date.month()) << '-' << std::setw(2) << static_cast<unsigned>(date.day()) << 'T'
         << std::setw(2) << time_of_day.hours().count() << ':' << std::setw(2) << time_of_day.minutes().count() << ':'
         << std::setw(2) << time_of_day.seconds().count() << '.' << std::setw(fractional_second_digits)
         << time_of_day.subseconds().count() << 'Z';
  return output.str();
}

}  // namespace astl
#endif  // ASTL_WALL_CLOCK_HPP_
