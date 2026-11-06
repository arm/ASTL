// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_CLOCK_CORRELATION_HPP_
#define SCMI_CLOCK_CORRELATION_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

#include "astl/astl.h"
#include "common/clock_correlation.hpp"
#include "scmi/scmi_constants.hpp"
#include "scmi/scmi_ioctl_uapi.hpp"

namespace astl::scmi_detail {

// Number of attempts, including failed reads and duplicate firmware timestamps.
inline constexpr size_t kClockCorrelationReadAttempts = 20;

/** Half the active firmware interval, rounded up to whole nanoseconds. */
auto ClockCorrelationReadSpacing(scmi_tlm_update_interval interval)
    -> std::expected<std::chrono::nanoseconds, astl_status_code>;

/** Configuration-local minimum signed raw-minus-native offset. */
class ClockCorrelationMinimum {
 public:
  explicit ClockCorrelationMinimum(kilohertz rate) : _rate{rate} {}

  // All valid pairs compete, including duplicates. Equal offsets retain the first pair.
  auto ConsiderSample(HwClockTicks native, ProcessedSampleTimestamp raw) -> astl_status_code;
  auto Correlation() const -> std::expected<OperationClockCorrelation, astl_status_code>;
  auto Count() const -> size_t { return _count; }

 private:
  kilohertz                                _rate;
  std::optional<OperationClockCorrelation> _best;
  HwClockTicks                             _last_native{};
  size_t                                   _count{};
};

}  // namespace astl::scmi_detail

#endif  // SCMI_CLOCK_CORRELATION_HPP_
