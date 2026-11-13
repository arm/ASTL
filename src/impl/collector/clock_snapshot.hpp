// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTOR_CLOCK_SNAPSHOT_HPP_
#define COLLECTOR_CLOCK_SNAPSHOT_HPP_

#include <cstdint>
#include <string_view>

#include "astl_cxx_compat.hpp"
#include "astl_logger.hpp"
#include "collector/collection_configuration.hpp"
#include "common/clock_correlation.hpp"

namespace astl {

/** Capture one reference shared by a configured batch. The caller holds its collector mutex. */
inline auto CaptureOperationClockSnapshot(const astl::compat::optional<CollectionConfiguration>& configuration,
                                          uint64_t (*native_now)(), std::string_view collector_name)
    -> ClockCorrelationMap {
  if (!configuration) {
    ASTL_LOG_WARNING("{}::GetNativeClockSnapshot called without configuration; returning empty map", collector_name);
    return {};
  }
  const auto          native_tick = native_now();
  const auto          raw_now     = ClockMonotonicRaw::now();
  ClockCorrelationMap result;
  for (const auto& operation : configuration->Operations().operationsOnSample) {
    result[operation->GetId()] = OperationClockCorrelation{raw_now, native_tick, MakeTickRatio<SampleMicroseconds>()};
  }
  return result;
}

}  // namespace astl
#endif  // COLLECTOR_CLOCK_SNAPSHOT_HPP_
