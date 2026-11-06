// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <map>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "astl_logger.hpp"
#include "scmi/scmi_clock_correlation.hpp"
#include "scmi/scmi_ioctl_collector.hpp"
#include "scmi/scmi_read_operation.hpp"

namespace astl {
namespace {

using IntervalKey   = std::pair<bool, uint32_t>;
using IntervalCache = std::map<IntervalKey, std::chrono::nanoseconds>;

struct CorrelationRead {
  const ScmiReadOperation*              operation{};
  std::vector<OperationId>              operations;
  scmi_detail::ClockCorrelationMinimum  minimum;
  std::chrono::nanoseconds              spacing;
  std::chrono::steady_clock::time_point previous_read;
  size_t                                failures{};
  astl_status_code                      last_error{ASTL_STATUS_BAD_CONFIGURATION};
};

auto ReadSpacing(IScmiIoctlInterface& interface, const ScmiReadOperation& operation, IntervalCache& intervals)
    -> std::expected<std::chrono::nanoseconds, astl_status_code> {
  scmi_tlm_de_info info{};
  const auto       info_status = interface.GetDataEventInfo(operation.scmi_data_event_id, info);
  if (info_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{info_status};
  }
  const auto rate = info.ts_rate != 0 ? info.ts_rate : kDefaultScmiTimestampRate;
  if (operation.tstamp_rate == 0 || rate != operation.tstamp_rate) {
    return std::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
  }
  const bool        grouped = SCMI_TLM_DEINFO_HAS_GROUP(info.flags) != 0;
  const IntervalKey key{grouped, grouped ? info.grp_id : 0};
  if (const auto found = intervals.find(key); found != intervals.end()) {
    return found->second;
  }
  scmi_tlm_config config{};
  config.flags             = grouped ? SCMI_TLM_CONFIG_GROUP : 0;
  config.grp_id            = key.second;
  const auto config_status = interface.GetConfig(config);
  if (config_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{config_status};
  }
  const auto spacing = scmi_detail::ClockCorrelationReadSpacing(config.active);
  if (spacing) {
    intervals.emplace(key, *spacing);
  }
  return spacing;
}

auto PrepareCorrelationReads(IScmiIoctlInterface& interface, const OperationSequence& operations)
    -> std::expected<std::vector<CorrelationRead>, astl_status_code> {
  std::vector<CorrelationRead>                reads;
  std::unordered_map<ScmiDataEventId, size_t> by_data_event;
  IntervalCache                               intervals;
  for (const auto& operation_ptr : operations) {
    const auto* operation = dynamic_cast<const ScmiReadOperation*>(operation_ptr.get());
    if (operation == nullptr) {
      continue;
    }
    if (const auto found = by_data_event.find(operation->scmi_data_event_id); found != by_data_event.end()) {
      auto& read = reads[found->second];
      if (read.operation->tstamp_rate != operation->tstamp_rate) {
        return std::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
      }
      read.operations.push_back(operation->GetId());
      continue;
    }
    const auto spacing = ReadSpacing(interface, *operation, intervals);
    if (!spacing) {
      ASTL_LOG_ERROR("SCMI clock correlation: unable to read a valid active interval for DE_ID {:08X}: {}",
                     operation->scmi_data_event_id, astlStatusString(spacing.error()));
      return std::unexpected{spacing.error()};
    }
    by_data_event.emplace(operation->scmi_data_event_id, reads.size());
    reads.push_back(CorrelationRead{
        operation, {operation->GetId()}, scmi_detail::ClockCorrelationMinimum{operation->tstamp_rate}, *spacing, {}});
  }
  return reads;
}

auto CaptureCorrelationRead(IScmiIoctlInterface& interface, CorrelationRead& read, bool wait) -> astl_status_code {
  if (wait) {
    const auto elapsed = std::chrono::steady_clock::now() - read.previous_read;
    if (elapsed < read.spacing) {
      std::this_thread::sleep_for(read.spacing - elapsed);
    }
  }
  read.previous_read = std::chrono::steady_clock::now();
  scmi_tlm_de_sample sample{};
  const auto         status  = interface.ReadDataEventValue(read.operation->scmi_data_event_id, sample);
  const auto         raw_now = ClockMonotonicRaw::now();
  if (status != ASTL_STATUS_SUCCESS || sample.tstamp == 0) {
    ++read.failures;
    read.last_error = status == ASTL_STATUS_SUCCESS ? ASTL_STATUS_BAD_CONFIGURATION : status;
    return ASTL_STATUS_SUCCESS;
  }
  const auto sample_status = read.minimum.ConsiderSample(sample.tstamp, raw_now);
  if (sample_status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("SCMI clock correlation: invalid or backward timestamp for DE_ID {:08X}",
                   read.operation->scmi_data_event_id);
  }
  return sample_status;
}

auto MakeMinimumCorrelations(const std::vector<CorrelationRead>& reads)
    -> std::expected<ClockCorrelationMap, astl_status_code> {
  ClockCorrelationMap result;
  for (const auto& read : reads) {
    ASTL_LOG_DEBUG("SCMI clock correlation DE_ID {:08X}: attempts={} distinct={} failures={} spacing_ns={}",
                   read.operation->scmi_data_event_id, scmi_detail::kClockCorrelationReadAttempts, read.minimum.Count(),
                   read.failures, read.spacing.count());
    if (read.failures != 0) {
      ASTL_LOG_WARNING("SCMI clock correlation DE_ID {:08X}: {} failed reads; {} distinct samples available",
                       read.operation->scmi_data_event_id, read.failures, read.minimum.Count());
    }
    if (read.minimum.Count() == 0) {
      return std::unexpected{read.last_error};
    }
    const auto correlation = read.minimum.Correlation();
    if (!correlation) {
      return std::unexpected{correlation.error()};
    }
    for (const auto operation_id : read.operations) {
      result.emplace(operation_id, *correlation);
    }
  }
  return result;
}

}  // namespace

auto ScmiIoctlCollector::GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock lock{_collection_mutex};
  if (!_configuration) {
    ASTL_LOG_WARNING("ScmiIoctlCollector::GetNativeClockSnapshot called without configuration; returning empty map");
    return {};
  }
  const auto& operations = _configuration->Operations().operationsOnSample;
  if (_use_software_clock_timestamps) {
    ClockCorrelationMap result;
    for (const auto& operation : operations) {
      if (const auto* scmi_operation = dynamic_cast<const ScmiReadOperation*>(operation.get())) {
        const auto raw_now = ClockMonotonicRaw::now();
        result.emplace(scmi_operation->GetId(),
                       OperationClockCorrelation{
                           raw_now, static_cast<HwClockTicks>(raw_now.time_since_epoch().count()), {1, 1}
        });
      }
    }
    return result;
  }
  auto reads = PrepareCorrelationReads(*_scmi_ioctl_interface, operations);
  if (!reads) {
    return std::unexpected{reads.error()};
  }
  for (size_t attempt = 0; attempt < scmi_detail::kClockCorrelationReadAttempts; ++attempt) {
    for (auto& read : *reads) {
      const auto status = CaptureCorrelationRead(*_scmi_ioctl_interface, read, attempt != 0);
      if (status != ASTL_STATUS_SUCCESS) {
        return std::unexpected{status};
      }
    }
  }
  return MakeMinimumCorrelations(*reads);
}

}  // namespace astl
