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

#ifndef RAW_METRIC_HPP_
#define RAW_METRIC_HPP_

#include <span>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "i_metric.hpp"

namespace astl {

/**
 * @brief Base class for raw metric types that process sampled data.
 *
 * RawMetric provides default behavior for capability checking and operation setup.
 * Specific metric types like sampled, delta, or residency should inherit from this class
 * and implement sample processing and summarization.
 */
class RawMetric : public IMetric {
 public:
  ~RawMetric() override = default;

  RawMetric()                  = delete;  // Default constructor is deleted to prevent instantiation without parameters.
  RawMetric(const RawMetric &) = default;
  RawMetric &operator=(const RawMetric &) = default;
  RawMetric(RawMetric &&)                 = default;
  RawMetric &operator=(RawMetric &&)      = default;

  /**
   * @brief Construct a RawMetric with specified name, description, units, and value type.
   *
   * @param name The name of the metric.
   * @param description A brief description of the metric.
   * @param units The units of measurement for this metric.
   * @param value_type The type of values this metric will process.
   */
  explicit RawMetric(const char *name, const char *description, astl_units_t units, astl_value_type_t value_type,
                     astl_metric_type_t metric_type)
      : _name(name), _description(description), _units(units), _value_type(value_type), _metric_type(metric_type) {
    // Initialize logger header
    // TODO (ASTL-58): When the output manager is implemented raw_sample_logger will be part of the OutputManager.
    _raw_sample_logger.LogInfo("Metric, Description, Units, Raw-Value, Timestamp \n");
  }

  /**
   * @brief Check if the Capabilities are met for this metric.
   * This method can be overridden by derived classes to implement specific capability checks.
   *
   * @param capabilities The capabilities to check against.
   * @return true if the capabilities are met, false otherwise.
   */
  bool CheckCapabilities(const Capabilities &capabilities) const override {
    (void)capabilities;
    return true;  // Default implementation, can be overridden by derived classes
  }

  /**
   * @brief Get the Operations required to the metric.
   * The API determine the collector protocol from the Metric Config and create the Operations.
   *
   * @return OperationSequence
   */
  std::expected<OperationSequence, astl_status_code> GetOperations() const override {
    // Default implementation returns an empty sequence
    return OperationSequence{};
  }

  astl_status_code ReceiveSample(const SampledData &sample) override = 0;

  /**
   * @brief Return a view of the samples received by this metric
   */
  std::span<const SampledData> GetSamples() const override = 0;

  /**
   * @brief Reset the metric state, dropping all collected samples
   */
  void Reset() override = 0;

  astl_status_code Summarize() override = 0;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */

  astl_status_code GetProperties(astl_metric_properties_t *properties) const final {
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

 protected:
  /**
   * @brief Check if the sample's value type matches the metric's expected type
   * @param sample The sample data to validate
   * @return ASTL_STATUS_SUCCESS if types match, ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE if not
   */
  astl_status_code CheckSampleValueType(const SampledData &sample) const {
    const auto sample_type = sample.value.ToAstlUnionValue().second;
    if (sample_type != _value_type) {
      ASTL_LOG_ERROR("Metric {}: received sample with type {} but expected type {}", _name.c_str(), sample_type,
                     _value_type);
      return ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE;
    }
    return ASTL_STATUS_SUCCESS;
  }

  /**
   * @brief Log a raw sample to the raw sample logger
   * @param sample The sample data to log
   */
  void LogRawSample(const SampledData &sample) {
    // LOG : Metric, Description, Units, Raw-Value, Timestamp
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp.time_since_epoch()).count();
    _raw_sample_logger.LogInfo("{}, {}, {}, {}, {} \n", _name, _description, _units, sample.value, timestamp);
  }

  // Member variables for metrics are protected to allow access in derived classes.
  // NOLINTBEGIN - Disable clang-tidy checks for protected members.
  // The common parameters used by all metric types like formula, mask go in here.
  std::string        _name;
  std::string        _description;
  astl_units_t       _units;
  astl_value_type_t  _value_type;
  astl_metric_type_t _metric_type;

  // Create a Logger instance explicitly to log raw samples
  // TODO (ASTL-58): When the output manager is implemented raw_sample_logger will be part of the OutputManager.
  astl::Logger _raw_sample_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                                  false /* No default formatting */, "raw_samples.log"};

  // NOLINTEND - End of clang-tidy checks for protected members.
};  // End of RawMetric class

}  // namespace astl

#endif  // RAW_METRIC_HPP_
