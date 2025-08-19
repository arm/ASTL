/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "event_metric.hpp"

#include <chrono>
#include <variant>

#include "astl_logger.hpp"

namespace astl {

void EventMetric::Initialize() {
  _events.clear();
  _summary.counts.clear();
}

void EventMetric::Reset() { Initialize(); }

astl_status_code EventMetric::CheckAndStoreEvent(const SampledData& sample) {
  // Check if the value can be converted to a string
  if (!sample.value.IsStringConvertible()) {
    ASTL_LOG_ERROR("EventMetric {}: received sample that cannot be converted to string", _name.c_str());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  // Convert value to string
  std::string event_str;
  if (!sample.value.ToStringValue(event_str)) {
    ASTL_LOG_ERROR("EventMetric {}: failed to convert sample to string", _name.c_str());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }

  // Create timeline entry
  _events.push_back(EventData{.description = event_str, .timestamp = sample.timestamp});

  // Update counts
  _summary.counts[event_str]++;

  // Log timeline entry (timestamp in microseconds)
  auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp.time_since_epoch()).count();
  _event_timeline_logger.LogInfo("{}, {}, {}\n", _name, event_str, ts_us);

  return ASTL_STATUS_SUCCESS;
}

astl_status_code EventMetric::ReceiveSample(const SampledData& sample) { return CheckAndStoreEvent(sample); }

astl_status_code EventMetric::Summarize() {
  // Log summary counts
  for (const auto& [event, count] : _summary.counts) {
    _event_summary_logger.LogInfo("{}, {}, {}\n", _name, event, count);
  }
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl
