// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "scmi/scmi_clock_correlation.hpp"

#include <limits>
#include <numeric>

#include "astl_cxx_compat.hpp"

namespace astl::scmi_detail {
namespace {

constexpr uint64_t kNanosecondsPerMillisecond      = 1'000'000;
constexpr int64_t  kDecimalBase                    = 10;
constexpr int64_t  kHalfIntervalMultiplier         = 5;
constexpr int64_t  kHalfIntervalNanosecondExponent = 8;
constexpr int64_t  kMaxNanosecondExponent          = 18;
constexpr int64_t  kMinNanosecondExponent          = -11;

auto HasSmallerOffset(HwClockTicks native, ProcessedSampleTimestamp raw, const OperationClockCorrelation& best,
                      kilohertz rate) -> bool {
  const auto raw_ns      = raw.time_since_epoch().count();
  const auto best_raw_ns = best.raw_monotonic_at_start.time_since_epoch().count();
  // ConsiderSample validates nondecreasing native timestamps, so an earlier raw time wins.
  if (raw_ns < best_raw_ns) {
    return true;
  }
  // Ordered signed inputs permit a full-width unsigned difference without signed overflow.
  const auto raw_delta           = static_cast<uint64_t>(raw_ns) - static_cast<uint64_t>(best_raw_ns);
  const auto native_delta        = native - best.native_at_start;
  const auto raw_milliseconds    = raw_delta / kNanosecondsPerMillisecond;
  const auto native_milliseconds = native_delta / rate;
  if (raw_milliseconds != native_milliseconds) {
    return raw_milliseconds < native_milliseconds;
  }
  // Compare the fractional milliseconds exactly. Each remainder product is less
  // than 1,000,000 * UINT32_MAX, so uint64_t suffices on every supported compiler.
  return ((raw_delta % kNanosecondsPerMillisecond) * rate) < ((native_delta % rate) * kNanosecondsPerMillisecond);
}

}  // namespace

auto ClockCorrelationReadSpacing(scmi_tlm_update_interval interval)
    -> astl::compat::expected<std::chrono::nanoseconds, astl_status_code> {
  if (interval.secs == 0) {
    return astl::compat::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
  }
  auto       nanoseconds = static_cast<int64_t>(interval.secs) * kHalfIntervalMultiplier;
  const auto exponent    = static_cast<int64_t>(interval.exp) + kHalfIntervalNanosecondExponent;
  if (exponent > kMaxNanosecondExponent) {
    return astl::compat::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
  }
  if (exponent < kMinNanosecondExponent) {
    return std::chrono::nanoseconds{1};
  }
  for (int64_t digit = 0; digit < exponent; ++digit) {
    if (nanoseconds > std::numeric_limits<int64_t>::max() / kDecimalBase) {
      return astl::compat::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
    }
    nanoseconds *= kDecimalBase;
  }
  for (int64_t digit = 0; digit > exponent; --digit) {
    nanoseconds = (nanoseconds / kDecimalBase) + (nanoseconds % kDecimalBase != 0 ? 1 : 0);
  }
  return std::chrono::nanoseconds{nanoseconds};
}

auto ClockCorrelationMinimum::ConsiderSample(HwClockTicks native, ProcessedSampleTimestamp raw) -> astl_status_code {
  if (_rate == 0 || native == 0 || (_best && native < _last_native)) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  if (!_best || HasSmallerOffset(native, raw, *_best, _rate)) {
    // Reduce the ratio before later normalization multiplies elapsed native ticks.
    const auto divisor = std::gcd(kNanosecondsPerMillisecond, static_cast<uint64_t>(_rate));
    _best              = OperationClockCorrelation{
        raw,
        native,
                     {static_cast<int64_t>(kNanosecondsPerMillisecond / divisor), static_cast<int64_t>(_rate / divisor)}
    };
  }
  if (native != _last_native) {
    ++_count;
  }
  _last_native = native;
  return ASTL_STATUS_SUCCESS;
}

auto ClockCorrelationMinimum::Correlation() const
    -> astl::compat::expected<OperationClockCorrelation, astl_status_code> {
  if (!_best) {
    return astl::compat::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
  }
  return *_best;
}

}  // namespace astl::scmi_detail
