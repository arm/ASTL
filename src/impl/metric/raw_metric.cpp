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

#include "raw_metric.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <optional>
#include <string>

#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace astl {

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any IRawSampleSink.
 */
void RawMetric::SetProcessedSampleSink(IProcessedSampleSink *processed_sample_sink) {
  std::scoped_lock lock{_metric_mutex};
  _processed_sample_sink = processed_sample_sink;
};

astl_status_code RawMetric::SinkProcessedSample(const ProcessedSampledData &processed_sample) {
  std::lock_guard<std::mutex> lock(_metric_mutex);  // Ensure thread-safe access to the processed sample sink

  // Forward the processed sample to the sink
  ProcessedSampledData sample = processed_sample;
  return _processed_sample_sink ? _processed_sample_sink->SinkProcessedSamples(_target, this, {&sample, 1})
                                : ASTL_STATUS_BAD_CONFIGURATION;
};

astl_status_code RawMetric::GetProperties(astl_metric_properties_t *properties) const {
  if (properties == nullptr) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  // Fill in the metric properties structure
  properties->_size = sizeof(astl_metric_properties_t);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  properties->_handle                = static_cast<astl_metric_handle_t>(const_cast<RawMetric *>(this));
  properties->_name                  = _name.c_str();
  properties->_description           = _description.c_str();
  properties->_min_sampling_interval = 0;  // TODO(ASTL-40): Set appropriate minimum sampling interval from config.
  properties->_units                 = _units;
  properties->_value_type            = _value_type;
  properties->_metric_type           = _metric_type;

  return ASTL_STATUS_SUCCESS;
}

astl_status_code RawMetric::CheckSampleValueType(const RawSampledData &raw_sample) const {
  const auto sample_type = raw_sample.value.ToAstlUnionValue().second;
  if (sample_type != _value_type) {
    ASTL_LOG_ERROR("Metric {}: received sample with type {} but expected type {}", _name.c_str(), sample_type,
                   _value_type);
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }
  return ASTL_STATUS_SUCCESS;
}

void RawMetric::LogRawSample(const RawSampledData &raw_sample) {
  // LOG : Metric, Description, Units, Raw-Value, Timestamp
  auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(raw_sample.timestamp.time_since_epoch()).count();
  _raw_sample_logger.LogInfo("{}, {}, {}, {}, {} \n", _name, _description, _units, raw_sample.value, timestamp);
}

std::string RawMetric::SanitizeMetricNameForFilename(const std::string &name) {
  std::string sanitized = name;

  // Replace spaces and other problematic characters with underscores
  std::transform(sanitized.begin(), sanitized.end(), sanitized.begin(), [](char chr) {
    if (std::isalnum(chr) || chr == '_' || chr == '-') {
      return chr;  // Keep alphanumeric, underscore, and hyphen
    }
    return '_';  // Replace everything else with underscore
  });

  // Remove consecutive underscores
  auto new_end =
      std::unique(sanitized.begin(), sanitized.end(), [](char prev, char curr) { return prev == '_' && curr == '_'; });
  sanitized.erase(new_end, sanitized.end());

  // Remove leading/trailing underscores
  sanitized.erase(0, sanitized.find_first_not_of('_'));
  sanitized.erase(sanitized.find_last_not_of('_') + 1);

  // Ensure we have a valid filename (not empty)
  if (sanitized.empty()) {
    sanitized = "metric";
  }

  return sanitized;
}
}  // namespace astl
