// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <limits>

#include "common/monotonic_raw_clock.hpp"
#include "numa/numa_collector.hpp"
namespace astl {
namespace {
auto NumaNativeNow() -> uint64_t {
  return static_cast<uint64_t>(
      std::chrono::time_point_cast<SampleMicroseconds>(ClockMonotonicRaw::now()).time_since_epoch().count());
}
auto NumaValue(const NumaReadOperation& read, const numa::Counts& delta) -> std::optional<AstlValue> {
  const auto index = static_cast<size_t>(read.field);
  if (index < numa::kFields.size()) {
    return AstlValue{delta.at(index)};
  }
  if (delta[4] > std::numeric_limits<uint64_t>::max() - delta[3]) {
    return std::nullopt;
  }
  const auto total = delta[3] + delta[4];
  if (read.field == NumaField::ALLOCATIONS) {
    return AstlValue{total};
  }
  // Raw percentages are basis points (10000 = 100%); the metric formula scales to percent.
  if (read.field != NumaField::LOCAL_PERCENT && read.field != NumaField::REMOTE_PERCENT) {
    return std::nullopt;
  }
  const auto part = read.field == NumaField::LOCAL_PERCENT ? delta[3] : delta[4];
  return AstlValue{total == 0
                       ? uint64_t{0}
                       : static_cast<uint64_t>(static_cast<long double>(numa::kBasisPointsPerWhole) * part / total)};
}
}  // namespace
auto NumaCollector::GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock    lock{_collection_mutex};
  ClockCorrelationMap result;
  if (!_configuration) {
    return result;
  }
  const auto now  = ClockMonotonicRaw::now();
  const auto tick = NumaNativeNow();
  for (const auto& operation : _configuration->Operations().operationsOnSample) {
    result[operation->GetId()] = OperationClockCorrelation{now, tick, MakeTickRatio<SampleMicroseconds>()};
  }
  return result;
}
auto NumaCollector::ExecuteCollectionOperations(const OperationSequence& operations) -> astl_status_code {
  if (operations.empty()) {
    return ASTL_STATUS_SUCCESS;
  }
  auto snapshot = numa::Read(_files, _nodes);
  if (!snapshot) {
    _previous.clear();
    return snapshot.error();
  }
  std::vector<RawSampledData> samples;
  const auto                  tick = NumaNativeNow();
  for (const auto& operation : operations) {
    const auto* read = dynamic_cast<const NumaReadOperation*>(operation.get());
    if (!read) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    auto&      previous = _previous[read->GetId()];
    const auto delta    = numa::Delta(previous, *snapshot, read->node);
    previous            = *snapshot;
    if (delta) {
      if (auto value = NumaValue(*read, *delta)) {
        samples.emplace_back(read->GetId(), std::move(*value), tick);
      }
    }
  }
  return !samples.empty() && _sample_sink ? _sample_sink->SinkRawSamples(_configuration->Target(), samples)
                                          : ASTL_STATUS_SUCCESS;
}
}  // namespace astl
