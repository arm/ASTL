// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/counter.hpp"

#include "common/string_pool.hpp"

namespace astl {

Counter::Counter(const MetricConfig *config, const ITarget *target) : RawMetric{config, target, nullptr} {}

/**
 * @brief Assign values such as name, units, etc to the given properties pointer.
 */
auto Counter::GetProperties(astl_counter_properties_t *properties) const -> astl_status_code {
  // properties->_size  : set by API caller.
  // properties->_handle : set by caller (MetricManager)
  properties->_name                  = GetInternedString(_configuration->Name());
  properties->_description           = GetInternedString(_configuration->Description());
  properties->_mask                  = 0;
  properties->_min_sampling_interval = 0;
  properties->_units                 = _configuration->Units();
  properties->_formula               = "";
  properties->_value_type            = _configuration->ValueType();
  properties->_counter_type          = ASTL_COUNTER_TYPE_COUNT;
  return ASTL_STATUS_SUCCESS;
}

/**
 * @brief Get the Operations required to the metric.
 * The API determine the collector protocol from the Metric Config and create the Operations.
 *
 * @return OperationSequence
 */
auto Counter::GetOperations() -> std::expected<OperationSequence, astl_status_code> {
  return BuildOperations(_configuration->GetOperationBuilder(), _target);
}

auto Counter::ReceiveRawSample(const RawSampledData &raw_sample) -> astl_status_code {
  // if the counter has an expected value type, check that the sample matches it
  if (ASTL_VALUE_UNKNOWN != _configuration->ValueType()) {
    auto type_check_result = CheckSampleValueType(raw_sample);
    if (type_check_result != ASTL_STATUS_SUCCESS) {
      return type_check_result;
    }
  }

  // Log the raw sample using the base class method
  LogRawSample(raw_sample);
  ProcessedSampledData processed_sample{raw_sample.value, raw_sample.timestamp};
  // fan-out to manager / external sinks
  SinkProcessedSample(processed_sample);
  return ASTL_STATUS_SUCCESS;
}

auto Counter::Reset() -> void {}

}  // namespace astl