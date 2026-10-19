// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cmath>

#include "astl_logger.hpp"
#include "common/monotonic_raw_clock.hpp"
#include "windows/windows_system_collector.hpp"

namespace astl {
namespace {
constexpr double kPercentageScale = 100.0;

auto NativeNow() -> uint64_t {
  if (ClockMonotonicRaw::TestTimeEnabled()) {
    return static_cast<uint64_t>(
        std::chrono::time_point_cast<SampleMicroseconds>(ClockMonotonicRaw::now()).time_since_epoch().count());
  }
  return static_cast<uint64_t>(
      std::chrono::time_point_cast<SampleMicroseconds>(std::chrono::steady_clock::now()).time_since_epoch().count());
}

struct BatchSnapshot {
  std::unordered_map<std::string, std::optional<double>> cpu;
  WindowsMemorySnapshot                                  memory{};
};

struct BatchRequirements {
  bool cpu{false};
  bool memory{false};
};

auto GetBatchRequirements(const OperationSequence& operations) -> std::expected<BatchRequirements, astl_status_code> {
  BatchRequirements required;
  for (const auto& operation : operations) {
    const auto* read = dynamic_cast<const WindowsReadOperation*>(operation.get());
    if (!read) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    required.cpu |= read->field == WindowsSystemField::CPU_UTILIZATION;
    required.memory |= read->field != WindowsSystemField::CPU_UTILIZATION;
  }
  return required;
}

auto ReadBatch(IWindowsSystemApi& api, const BatchRequirements& required)
    -> std::expected<BatchSnapshot, astl_status_code> {
  BatchSnapshot snapshot;
  if (required.cpu) {
    auto cpu = api.ReadCpu();
    if (!cpu) {
      return std::unexpected(cpu.error());
    }
    snapshot.cpu = std::move(*cpu);
  }
  if (required.memory) {
    const auto memory = api.ReadMemory();
    if (!memory) {
      return std::unexpected(memory.error());
    }
    if (memory->total == 0 || memory->available > memory->total) {
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    snapshot.memory = *memory;
  }
  return snapshot;
}

auto CpuValue(const BatchSnapshot& snapshot, const std::string& instance) -> std::optional<AstlValue> {
  const auto found = snapshot.cpu.find(instance);
  if (found == snapshot.cpu.end() || !found->second || !std::isfinite(*found->second) || *found->second < 0 ||
      *found->second > kPercentageScale) {
    return std::nullopt;
  }
  return AstlValue{*found->second};
}

auto OperationValue(const WindowsReadOperation& read, const BatchSnapshot& snapshot) -> std::optional<AstlValue> {
  const auto& memory = snapshot.memory;
  switch (read.field) {
    case WindowsSystemField::CPU_UTILIZATION:
      return CpuValue(snapshot, read.instance);
    case WindowsSystemField::MEMORY_TOTAL:
      return AstlValue{memory.total};
    case WindowsSystemField::MEMORY_USED:
      return AstlValue{memory.total - memory.available};
    case WindowsSystemField::MEMORY_UTILIZATION:
      return AstlValue{kPercentageScale * static_cast<double>(memory.total - memory.available) /
                       static_cast<double>(memory.total)};
  }
  return std::nullopt;
}
}  // namespace

auto WindowsSystemCollector::GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock lock{_collection_mutex};
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING(
        "WindowsSystemCollector::GetNativeClockSnapshot called without configuration; returning empty map");
    return {};
  }

  const uint64_t native_now = NativeNow();
  const auto     raw_now    = ClockMonotonicRaw::now();

  ClockCorrelationMap result;
  for (const auto& operation_ptr : _configuration->Operations().operationsOnSample) {
    result[operation_ptr->GetId()] =
        OperationClockCorrelation{raw_now, native_now, MakeTickRatio<SampleMicroseconds>()};
  }
  return result;
}

auto WindowsSystemCollector::ExecuteCollectionOperations(const OperationSequence& operations) -> astl_status_code {
  const auto required = GetBatchRequirements(operations);
  if (!required) {
    return required.error();
  }
  const auto snapshot = ReadBatch(*_api, *required);
  if (!snapshot) {
    return snapshot.error();
  }
  std::vector<RawSampledData> samples;
  const auto                  native_tick = NativeNow();
  for (const auto& operation : operations) {
    const auto& read = dynamic_cast<const WindowsReadOperation&>(*operation);
    if (auto value = OperationValue(read, *snapshot)) {
      samples.emplace_back(operation->GetId(), std::move(*value), native_tick);
    }
  }
  return !samples.empty() && _sample_sink != nullptr ? _sample_sink->SinkRawSamples(_configuration->Target(), samples)
                                                     : ASTL_STATUS_SUCCESS;
}
}  // namespace astl
