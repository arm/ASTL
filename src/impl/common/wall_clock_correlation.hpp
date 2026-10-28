// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WALL_CLOCK_CORRELATION_HPP_
#define WALL_CLOCK_CORRELATION_HPP_

#include <chrono>
#include <limits>
#include <optional>

#include "astl_wall_clock.hpp"
#include "common/monotonic_raw_clock.hpp"

namespace astl {

/** Fixed clock reference. Bounds only read latency, not NTP/PTP accuracy or subsequent drift. */
struct WallClockCorrelation {
  int64_t raw_ns{0};
  int64_t realtime_ns{0};
  int64_t uncertainty_ns{0};
  bool    operator==(const WallClockCorrelation&) const = default;
};

namespace detail {

inline auto MakeWallClockCorrelation(int64_t before, int64_t realtime, int64_t after)
    -> std::optional<WallClockCorrelation> {
  if (after < before || (before < 0 && after > std::numeric_limits<int64_t>::max() + before)) {
    return std::nullopt;
  }
  const auto elapsed = after - before;
  return WallClockCorrelation{before + (elapsed / 2), realtime, (elapsed / 2) + (elapsed % 2)};
}

#ifdef __linux__
inline auto TimespecToNanoseconds(const timespec& time) -> std::optional<int64_t> {
  constexpr int64_t factor           = 1'000'000'000;
  constexpr auto    min              = std::numeric_limits<int64_t>::min();
  constexpr auto    max              = std::numeric_limits<int64_t>::max();
  constexpr auto    first_second     = (min / factor) - 1;
  constexpr auto    first_nanosecond = (min % factor) + factor;
  const auto        seconds          = static_cast<int64_t>(time.tv_sec);
  const auto        nanos            = static_cast<int64_t>(time.tv_nsec);
  if (nanos < 0 || nanos >= factor || seconds < first_second || seconds > max / factor ||
      (seconds == first_second && nanos < first_nanosecond) || (seconds == max / factor && nanos > max % factor)) {
    return std::nullopt;
  }
  if (seconds == first_second) {
    return min + (nanos - first_nanosecond);
  }
  return (seconds * factor) + nanos;
}
#endif

}  // namespace detail

/** Bracket CLOCK_REALTIME with raw reads; use the midpoint and retain the read uncertainty. */
inline auto CaptureWallClockCorrelation() -> std::optional<WallClockCorrelation> {
#ifdef __linux__
  timespec before{};
  timespec wall{};
  timespec after{};
  if (::clock_gettime(CLOCK_MONOTONIC_RAW, &before) != 0 || ::clock_gettime(CLOCK_REALTIME, &wall) != 0 ||
      ::clock_gettime(CLOCK_MONOTONIC_RAW, &after) != 0) {
    return std::nullopt;
  }
  const auto start    = detail::TimespecToNanoseconds(before);
  const auto end      = detail::TimespecToNanoseconds(after);
  const auto realtime = detail::TimespecToNanoseconds(wall);
  if (!start || !end || !realtime) {
    return std::nullopt;
  }
  return detail::MakeWallClockCorrelation(*start, *realtime, *end);
#else
  const auto before = ClockMonotonicRaw::now().time_since_epoch().count();
  const auto wall =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto after = ClockMonotonicRaw::now().time_since_epoch().count();
  return detail::MakeWallClockCorrelation(before, wall, after);
#endif
}

/** Convert a sample using the shared checked nanosecond conversion. */
inline auto CorrelateWallClock(ProcessedSampleTimestamp timestamp, const WallClockCorrelation& reference)
    -> std::optional<int64_t> {
  return CorrelateWallClockNanoseconds(timestamp.time_since_epoch().count(), reference.raw_ns, reference.realtime_ns);
}

}  // namespace astl
#endif  // WALL_CLOCK_CORRELATION_HPP_
