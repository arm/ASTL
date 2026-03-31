// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MONOTONIC_RAW_CLOCK_HPP_
#define MONOTONIC_RAW_CLOCK_HPP_

#include <chrono>
#include <cstdint>

#ifdef __linux__
#  include <ctime>
#endif

#include "operation/operation.hpp"

namespace astl {

/**
 * @brief A C++ TrivialClock wrapping CLOCK_MONOTONIC_RAW on Linux.
 *
 * CLOCK_MONOTONIC_RAW is a Linux-specific monotonic clock not subject to NTP
 * adjustments, making it the ideal common reference for normalizing timestamps
 * from different hardware counters across collectors.
 *
 * On non-Linux platforms (e.g. Windows/MSVC) where CLOCK_MONOTONIC_RAW and
 * ::clock_gettime are unavailable, std::chrono::steady_clock is used as a
 * functionally equivalent fallback.
 *
 * Duration resolution is nanoseconds stored as int64_t (signed to allow offset arithmetic).
 */
struct ClockMonotonicRaw {
  using rep        = int64_t;
  using period     = std::nano;
  using duration   = std::chrono::duration<int64_t, std::nano>;
  using time_point = std::chrono::time_point<ClockMonotonicRaw, duration>;

  static constexpr bool is_steady = true;  // NOLINT(readability-identifier-naming) -- required by TrivialClock

  static auto now() noexcept -> time_point {  // NOLINT(readability-identifier-naming) -- required by TrivialClock
#ifdef __linux__
    struct timespec clock_ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &clock_ts);
    const int64_t nanos =
        (static_cast<int64_t>(clock_ts.tv_sec) * 1'000'000'000LL) + static_cast<int64_t>(clock_ts.tv_nsec);
#else
    // Fallback for non-Linux platforms: steady_clock is monotonic and not
    // subject to wall-clock adjustments, matching CLOCK_MONOTONIC_RAW semantics.
    const int64_t nanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
#endif
    return time_point{duration{nanos}};
  }
};

/** @brief Timestamp type for processed (normalized) samples — always CLOCK_MONOTONIC_RAW, nanosecond resolution. */
using ProcessedSampleTimestamp = std::chrono::time_point<ClockMonotonicRaw, std::chrono::duration<int64_t, std::nano>>;

/**
 * @brief Opaque type for a raw hardware clock tick count supplied by a collector.*/
using HwClockTicks = uint64_t;

}  // namespace astl

#endif  // MONOTONIC_RAW_CLOCK_HPP_
