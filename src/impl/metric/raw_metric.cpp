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
#include "metric/formula_builder.hpp"

namespace astl {

RawMetric::RawMetric(const MetricConfig *configuration, const ITarget *target,
                     IProcessedSampleSink *processed_sample_sink)
    : _configuration(configuration), _target(target) {
  SetProcessedSampleSink(processed_sample_sink);
  if (!_processed_sample_sink) {
    ASTL_LOG_ERROR("No processed sample sink set for metric '{}'. Sample will be dropped.", _configuration->Name());
  }

  // Initialize logger header
  // TODO (ASTL-58): When the output manager is implemented raw_sample_logger will be part of the OutputManager.
  _raw_sample_logger.LogInfo("Metric, Description, Units, Raw-Value, Timestamp \n");
}

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any IRawSampleSink.
 */
auto RawMetric::SetProcessedSampleSink(IProcessedSampleSink *processed_sample_sink) -> void {
  std::scoped_lock lock{_metric_mutex};
  _processed_sample_sink = processed_sample_sink;
}

auto RawMetric::Name() const -> std::string const & { return _configuration->Name(); }

auto RawMetric::SinkProcessedSample(const ProcessedSampledData &processed_sample) -> astl_status_code {
  std::lock_guard<std::mutex> lock(_metric_mutex);  // Ensure thread-safe access to the processed sample sink

  // Forward the processed sample to the sink
  ProcessedSampledData sample = processed_sample;
  return _processed_sample_sink ? _processed_sample_sink->SinkProcessedSamples(_target, this, {&sample, 1})
                                : ASTL_STATUS_BAD_CONFIGURATION;
};

auto RawMetric::GetProperties(astl_metric_properties_t *properties) const -> astl_status_code {
  if (properties == nullptr) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  // Fill in the metric properties structure
  properties->_size = sizeof(astl_metric_properties_t);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  properties->_handle                = static_cast<astl_metric_handle_t>(const_cast<RawMetric *>(this));
  properties->_name                  = _configuration->Name().c_str();
  properties->_description           = _configuration->Description().c_str();
  properties->_min_sampling_interval = 0;  // TODO(ASTL-40): Set appropriate minimum sampling interval from config.
  properties->_units                 = _configuration->Units();
  properties->_value_type            = _configuration->ValueType();
  properties->_metric_type           = _configuration->MetricType();
  properties->_category              = _configuration->Category();

  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Check if the Capabilities are met for this metric.
 * This method can be overridden by derived classes to implement specific capability checks.
 *
 * @param capabilities The capabilities to check against.
 * @return true if the capabilities are met, false otherwise.
 */
auto RawMetric::CheckCapabilities(const Capabilities &capabilities) const -> bool {
  // Default implementation assumes all capabilities are met.
  // Derived classes can override this method to implement specific checks.
  (void)capabilities;
  return true;
}

/**
 * @brief Get the Operations required to the metric.
 * The API determine the collector protocol from the Metric Config and create the Operations.
 *
 * @return OperationSequence
 */
auto RawMetric::GetOperations() -> std::expected<OperationSequence, astl_status_code> {
  // Default implementation returns an empty operation sequence.
  // Derived classes should override this method to provide actual operations.
  // TODO(ASTL-114) use a operation_builder to create operations from config
  return BuildOperations(_configuration->GetOperationBuilder(), _target);
}

auto RawMetric::CheckSampleValueType(const RawSampledData &raw_sample) const -> astl_status_code {
  const auto sample_type = raw_sample.value.ToAstlUnionValue().second;
  if (sample_type != _configuration->ValueType()) {
    ASTL_LOG_ERROR("Metric {}: received sample with type {} but expected type {}", _configuration->Name(), sample_type,
                   _configuration->ValueType());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }
  return ASTL_STATUS_SUCCESS;
}

auto RawMetric::LogRawSample(const RawSampledData &raw_sample) -> void {
  // LOG : Metric, Description, Units, Raw-Value, Timestamp
  auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(raw_sample.timestamp.time_since_epoch()).count();
  _raw_sample_logger.LogInfo("{}, {}, {}, {}, {} \n", _configuration->Name(), _configuration->Description(),
                             _configuration->Units(), raw_sample.value, timestamp);
}

auto RawMetric::ApplyFormula(const AstlValue &raw_value) const -> std::expected<AstlValue, astl_status_code> {
  // Use the formula from configuration
  return astl::ApplyFormula(_configuration->GetFormula(), raw_value);
}

auto RawMetric::SanitizeMetricNameForFilename(const std::string &name) -> std::string {
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
