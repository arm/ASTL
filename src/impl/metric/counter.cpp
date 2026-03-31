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
auto Counter::GetProperties(astl_counter_props_t *properties) const -> astl_status_code {
  // properties->size  : set by API caller.
  // properties->handle : set by caller (MetricManager)
  properties->name                  = GetInternedString(_configuration->Name());
  properties->description           = GetInternedString(_configuration->Description());
  properties->min_sampling_interval = 0;
  properties->units                 = _configuration->Units();
  // Expose the same composed formula used by metric post-processing for raw counter consumers.
  properties->formula = GetInternedString(FormatFormulaForApi(_configuration->GetFormula()));
  // Counter sample payloads are always reported in the raw/on-wire type expected by collection APIs.
  properties->value_type   = _configuration->InputValueType();
  properties->counter_type = ASTL_COUNTER_TYPE_COUNT;
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

auto Counter::ReceiveRawSample(const NormalizedSampledData &raw_sample) -> astl_status_code {
  // if the counter has an expected value type, check that the sample matches it
  if (ASTL_VALUE_UNKNOWN != _configuration->InputValueType()) {
    auto type_check_result = CheckSampleValueType(raw_sample);
    if (type_check_result != ASTL_STATUS_SUCCESS) {
      return type_check_result;
    }
  }

  // Log the raw sample using the base class method
  LogNormalizedSample(raw_sample);
  ProcessedSampledData processed_sample{raw_sample.value, raw_sample.timestamp};
  // fan-out to manager / external sinks
  SinkProcessedSample(processed_sample);
  return ASTL_STATUS_SUCCESS;
}

auto Counter::Reset() -> void {}

}  // namespace astl
