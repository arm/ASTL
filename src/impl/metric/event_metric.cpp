// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "event_metric.hpp"

#include <chrono>

#include "astl_logger.hpp"

namespace astl {

auto EventMetric::Initialize() -> void { _summary.counts.clear(); }

auto EventMetric::Reset() -> void { Initialize(); }

auto EventMetric::CheckAndStoreEvent(const NormalizedSampledData& raw_sample) -> astl_status_code {
  // Convert value to string
  std::string event_str;
  if (!raw_sample.value.ToStringValue(event_str)) {
    ASTL_LOG_ERROR("EventMetric {}: failed to convert sample to string", _configuration->Name());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  // TODO(fayben01): May need to process the raw sample further depending on requirements
  ProcessedSampledData processed_sampled_data{raw_sample.value, raw_sample.timestamp};

  SinkProcessedSample(processed_sampled_data);

  // Update counts
  _summary.counts[event_str]++;

  // Log timeline entry (timestamp in microseconds)
  auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(raw_sample.timestamp.time_since_epoch()).count();
  _event_timeline_logger.LogInfo("{}, {}, {}\n", _configuration->Name(), event_str, ts_us);

  return ASTL_STATUS_SUCCESS;
}

auto EventMetric::ReceiveRawSample(const NormalizedSampledData& raw_sample) -> astl_status_code {
  return CheckAndStoreEvent(raw_sample);
}

auto EventMetric::Summarize() -> astl_status_code {
  // Log summary counts
  for (const auto& [event, count] : _summary.counts) {
    _event_summary_logger.LogInfo("{}, {}, {}\n", _configuration->Name(), event, count);
  }
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
