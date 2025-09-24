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

#ifndef I_METRIC_HPP_
#define I_METRIC_HPP_

#include <expected>
#include <span>
#include <string>

#include "astl/astl.h"
#include "common/capabilities.hpp"
#include "common/i_processed_sample_sink.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "common/operation.hpp"

namespace astl {

struct ProcessedSampledData;
struct IProcessedSampleSink;

/**
 * @brief Abstract interface for all ASTL metric implementations.
 * All metric implementations should inherit from this class.
 */
struct IMetric {
  /**
   * @brief allow destroying metric instances by base class pointer
   */
  virtual ~IMetric() = default;

  IMetric()                           = default;
  IMetric(const IMetric &)            = default;
  IMetric &operator=(const IMetric &) = default;
  IMetric(IMetric &&)                 = default;
  IMetric &operator=(IMetric &&)      = default;

  /**
   * @brief check if the Capabilities are met for this metric
   * @param capabilities The capabilities to check against.
   * @return true if the capabilities are met, false otherwise.
   */
  virtual bool CheckCapabilities(const Capabilities &capabilities) const = 0;

  /**
   * @brief Get the Operations required to the metric.
   * The operations are used by the collector manager to determine what collector to be used and what samples are
   * collected.
   *
   * @return OperationSequence
   */
  virtual std::expected<OperationSequence, astl_status_code> GetOperations() = 0;

  /**
   * @brief Process the individual raw sample routed to metric.
   * This method is called by the Metric Manager to send individual samples to the metric plugin.
   * The Metric Manager ensures that the raw samples are monotonically increasing in time.
   *
   * @param sample The sample to process.
   * @return astl_status_code
   */
  virtual astl_status_code ReceiveRawSample(const RawSampledData &raw_sample) = 0;

  /*
   * @brief Set the destination for where processed sampled data should be sent.
   *       This is typically the MetricManager, but can be any IProcessedSampleSink.
   */
  virtual void SetProcessedSampleSink(IProcessedSampleSink *processed_sample_sink) = 0;

  /**
   * @brief Return a view of the samples processed by this metric
   */
  virtual std::span<const ProcessedSampledData> GetProcessedSamples() const = 0;

  /**
   * @brief Reset the metric state, dropping all collected samples
   */
  virtual void Reset() = 0;

  /**
   * @brief Summarize the metric.
   * Depending on the metric type, this may mean aggregating samples, calculating averages, etc.
   * This is called once all the samples are processed.
   */
  virtual astl_status_code Summarize() = 0;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   * TODO(ASTL-89): External C-interface data structure should be backward compatible.
   */
  virtual astl_status_code GetProperties(astl_metric_properties_t *properties) const = 0;

  /**
   * @brief Retrieve the metric's name as a string
   */
  virtual auto Name() const -> std::string const & = 0;
  /**
   * @brief Forward a single processed sample produced by this metric to the currently configured sink.
   *
   * This function is the final hand-off point in the metric processing pipeline. Implementations
   * typically perform minimal work here (e.g. validation, lightweight transformation, buffering)
   * before delegating to the `IProcessedSampleSink` previously installed via
   * `SetProcessedSampleSink`.
   *
   * Ownership / Lifetime:
   *  - The referenced `processed_sample` remains owned by the metric implementation; the sink MUST
   *    NOT retain references into the object beyond the duration of the call unless the metric
   *    provides a documented stability guarantee for its underlying storage (e.g. ring buffer).
   *  - If durable retention is required, the sink should copy the data it needs.
   *
   * Thread Safety:
   *  - Unless a specific metric documents otherwise, calls are expected to be serialized by the
   *    `MetricManager`. Implementations may assume single-threaded invocation unless explicitly
   *    stated.
   *
   * Ordering:
   *  - Samples are delivered in monotonically non-decreasing timestamp order (enforced earlier in
   *    the pipeline). Implementations may rely on this for incremental computations.
   *
   * Error Handling:
   *  - Returns `ASTL_STATUS_SUCCESS` on successful forwarding.
   *  - `ASTL_STATUS_NOT_INITIALIZED` if no sink has been configured.
   *  - `ASTL_STATUS_BAD_ARGUMENT` if the sample is structurally invalid (e.g. missing size
   *    initialization) – implementations may add additional validation criteria.
   *  - Other metric-specific error codes are permitted (e.g. capacity / overflow) and should be
   *    documented if introduced.
   *
   * Performance Considerations:
   *  - Keep this path allocation-free in the steady state to minimize perturbation of sampling
   *    cadence.
   *  - Avoid logging on the hot path except for error conditions.
   *
   * @param processed_sample The processed sample to be forwarded.
   * @return astl_status_code See Error Handling section above.
   */
  virtual astl_status_code SinkProcessedSample(const ProcessedSampledData &processed_sample) = 0;
};

}  // namespace astl

#endif  // I_METRIC_HPP_
