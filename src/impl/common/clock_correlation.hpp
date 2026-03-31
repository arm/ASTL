// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef CLOCK_CORRELATION_HPP_
#define CLOCK_CORRELATION_HPP_

#include <cstddef>
#include <cstdint>
#include <ratio>
#include <unordered_map>

#include "common/monotonic_raw_clock.hpp"
#include "operation/operation.hpp"

namespace astl {

/**
 * @brief Runtime representation of the ratio: 1 native tick expressed in CLOCK_MONOTONIC_RAW ticks (nanoseconds).
 *
 * Set by each collector to reflect its own native clock period and stored as a concrete `{num, den}`
 * pair so that `OperationClockCorrelation` remains a plain value type (no template parameters).
 *
 * Example: `SampleMicroseconds` has period `std::micro`, so
 *   `ratio_divide<micro, nano>` = `ratio<1000, 1>` → `{1000, 1}`.
 */
struct NativeToMonotonicRawRatio {
  intmax_t num{0};  ///< Numerator:   raw (ns) ticks per native tick
  intmax_t den{1};  ///< Denominator: always ≥ 1

  bool operator==(const NativeToMonotonicRawRatio&) const = default;
};

/**
 * @brief Construct a `NativeToMonotonicRawRatio` from a native duration type at compile time.
 *
 * Computes `std::ratio_divide<NativeDuration::period, std::nano>` and returns its
 * `num` and `den` as a runtime struct.
 *
 * @tparam NativeDuration  A `std::chrono::duration` specialization (e.g. `SampleMicroseconds`).
 */
template <typename NativeDuration>
constexpr auto MakeTickRatio() noexcept -> NativeToMonotonicRawRatio {
  using R = std::ratio_divide<typename NativeDuration::period, std::nano>;
  return {R::num, R::den};
}

/**
 * @brief A paired snapshot of CLOCK_MONOTONIC_RAW and a collector's native clock
 *        taken simultaneously for a single operation at collection-start time.
 *
 * The MetricManager uses this to translate each RawSampledData::raw_tick
 * (in the collector's native clock domain) to the common CLOCK_MONOTONIC_RAW
 * reference before forwarding samples to metric implementations as NormalizedSampledData.
 *
 * Formula applied at post-processing time:
 *   normalized_raw_ns =
 *       raw_monotonic_at_start_ns +
 *       (raw_tick - native_at_start) * ticks.num / ticks.den
 *
 * Every collector **must** supply `ticks` explicitly via `MakeTickRatio<NativeDuration>()`.
 */
struct OperationClockCorrelation {
  ProcessedSampleTimestamp  raw_monotonic_at_start;  ///< CLOCK_MONOTONIC_RAW snapshot taken at collection start
  HwClockTicks              native_at_start{0};      ///< Collector-native clock tick count at collection start
  NativeToMonotonicRawRatio ticks;                   ///< 1 native tick expressed in CLOCK_MONOTONIC_RAW (ns) ticks

  bool operator==(const OperationClockCorrelation&) const = default;
};

/**
 * @brief Normalize a collector-native hardware tick into the CLOCK_MONOTONIC_RAW domain.
 *
 * Uses the `ticks` ratio from `correlation` to convert the native elapsed tick count into
 * nanoseconds, producing a `ProcessedSampleTimestamp` suitable for `NormalizedSampledData`.
 *
 * @param native_tick Collector-native hardware clock tick read from `RawSampledData::raw_tick`.
 * @param correlation Per-operation anchor snapshot recorded at collection-start time.
 * @return CLOCK_MONOTONIC_RAW timestamp (nanosecond resolution).
 */
inline auto NormalizeToCorrelatedRawTimestamp(HwClockTicks                     native_tick,
                                              const OperationClockCorrelation& correlation) noexcept
    -> ProcessedSampleTimestamp {
  const int64_t native_elapsed_ticks =
      static_cast<int64_t>(native_tick) - static_cast<int64_t>(correlation.native_at_start);
  const int64_t raw_elapsed_ns    = native_elapsed_ticks * correlation.ticks.num / correlation.ticks.den;
  const int64_t normalized_raw_ns = correlation.raw_monotonic_at_start.time_since_epoch().count() + raw_elapsed_ns;
  return ProcessedSampleTimestamp{std::chrono::duration<int64_t, std::nano>{normalized_raw_ns}};
}

/** @brief Per-operation correlation map keyed by OperationId. */
using ClockCorrelationMap = std::unordered_map<OperationId, OperationClockCorrelation>;

// GCC 13 libstdc++'s std::expected::operator==(const U&) is unconstrained.
// This causes a hard error when trompeloeil probes `expected<ClockCorrelationMap,E> == nullptr`
// because the unconstrained template is selected and then fails in the body on
// `unordered_map<K,V> == std::nullptr_t`.  Providing a free operator== that accepts
// nullptr_t allows the expression to compile and correctly return false.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ <= 13
inline bool operator==(const ClockCorrelationMap& /*lhs*/, std::nullptr_t) noexcept { return false; }
inline bool operator==(std::nullptr_t, const ClockCorrelationMap& /*rhs*/) noexcept { return false; }
#endif

}  // namespace astl

#endif  // CLOCK_CORRELATION_HPP_
