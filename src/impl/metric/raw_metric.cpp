// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "raw_metric.hpp"

#include <algorithm>
#include <chrono>
#include <expected>
#include <string>

#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "common/string_pool.hpp"
#include "metric/formula_builder.hpp"
namespace astl {

RawMetric::RawMetric(const MetricConfig *configuration, const ITarget *target,
                     IProcessedSampleSink *processed_sample_sink)
    : _configuration(configuration), _target(target) {
  SetProcessedSampleSink(processed_sample_sink);
  if (!_processed_sample_sink) {
    ASTL_LOG_DEBUG(
        "Null processed_sample_sink for metric '{}' in constructor."
        " Samples will be dropped until SetProcessedSampleSink is called.",
        _configuration->Name());
  }

  // Initialize logger header
  // TODO (ASTL-58): When the output manager is implemented raw_sample_logger will be part of the OutputManager.
  _raw_sample_logger.LogInfo("Metric, Description, Units, Raw-Value, Timestamp(ns) \n");
}

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any IRawSampleSink.
 */
auto RawMetric::SetProcessedSampleSink(IProcessedSampleSink *processed_sample_sink) -> void {
  std::lock_guard lock{_metric_mutex};
  _processed_sample_sink = processed_sample_sink;
}

auto RawMetric::Name() const -> std::string const & { return _configuration->Name(); }

auto RawMetric::Id() const -> std::string const & { return _configuration->Id(); }

auto RawMetric::SinkProcessedSample(const ProcessedSampledData &processed_sample) -> astl_status_code {
  std::lock_guard lock(_metric_mutex);  // Ensure thread-safe access to the processed sample sink

  // Forward the processed sample to the sink
  ProcessedSampledData sample = processed_sample;
  if (!_processed_sample_sink) {
    ASTL_LOG_WARNING(
        "Metric {}: No processed sample sink set. Dropping processed sample with value {} at timestamp {}.",
        _configuration->Name(), sample.value,
        std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp.time_since_epoch()).count());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return _processed_sample_sink->SinkProcessedSamples(_target, this, {&sample, 1});
}

auto RawMetric::GetProperties(astl_metric_props_t *properties) const -> astl_status_code {
  if (properties == nullptr) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  // Fill in the metric properties structure
  properties->size = sizeof(astl_metric_props_t);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  properties->handle                = static_cast<astl_metric_handle_t>(const_cast<RawMetric *>(this));
  properties->name                  = GetInternedString(_configuration->Name());
  properties->description           = GetInternedString(_configuration->Description());
  properties->min_sampling_interval = 0;  // TODO(ASTL-40): Set appropriate minimum sampling interval from config.
  properties->units                 = _configuration->Units();
  properties->value_type            = _configuration->ValueType();
  properties->metric_type           = _configuration->MetricType();
  properties->identifier            = _configuration->Identifier();

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

auto RawMetric::CheckSampleValueType(const NormalizedSampledData &raw_sample) const -> astl_status_code {
  const auto sample_type = raw_sample.value.ToAstlUnionValue().second;
  // Validate against collector-facing raw input contract, not the post-formula output type.
  if (sample_type != _configuration->InputValueType()) {
    ASTL_LOG_ERROR("Metric {}: received sample with type {} but expected input type {}", _configuration->Name(),
                   sample_type, _configuration->InputValueType());
    return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
  }
  return ASTL_STATUS_SUCCESS;
}

auto RawMetric::LogNormalizedSample(const NormalizedSampledData &sample) -> void {
  // LOG : Metric, Description, Units, Raw-Value, Timestamp(ns)
  auto timestamp = sample.timestamp.time_since_epoch().count();
  _raw_sample_logger.LogInfo("{}, {}, {}, {}, {} \n", _configuration->Name(), _configuration->Description(),
                             _configuration->Units(), sample.value, timestamp);
}

auto RawMetric::ProcessPauseSample(ProcessedSampleTimestamp pause_timestamp) -> astl_status_code {
  // Log the pause boundary for diagnostics. Derived classes (e.g. DeltaMetric, ResidencyMetric)
  // override this to also reset internal accumulation state. EventMetric overrides it to record
  // the pause as a processed sample. The base implementation is intentionally a no-op sink-wise.
  ASTL_LOG_INFO("Metric {}: collection paused at {} ns", _configuration->Name(),
                pause_timestamp.time_since_epoch().count());
  return ASTL_STATUS_SUCCESS;
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
