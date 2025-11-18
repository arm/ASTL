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

#ifndef I_PROCESSED_SAMPLE_SINK_HPP_
#define I_PROCESSED_SAMPLE_SINK_HPP_

#include <chrono>
#include <span>
#include <utility>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "metric/i_metric.hpp"
#include "operation/operation.hpp"
#include "target.hpp"

namespace astl {

struct IMetric;

/**
 * @brief Represents a single processed metric sample.
 *
 * A processed sample is the value produced by a metric implementation after the raw
 * collection data has been interpreted (e.g. deltas computed, rates derived, aggregations updated).
 * Each sample carries an explicit timestamp. When a caller does not provide a timestamp the
 * construction path assigns the current steady clock time cast to the `SampleTimestamp` resolution.
 *
 * Thread-safety: This is a passive data object; concurrent reads are safe once fully constructed.
 */
struct ProcessedSampledData {
  ProcessedSampledData() = delete;

  /**
   * @brief Construct a processed sample with the current timestamp.
   * @param value Metric value (must hold a variant alternative valid for the metric).
   */
  explicit ProcessedSampledData(AstlValue value)
      : value{value},
        timestamp{std::chrono::time_point_cast<SampleTimestamp::duration>(std::chrono::steady_clock::now())} {}

  /**
   * @brief Construct a processed sample with an explicit timestamp.
   * @param value Metric value.
   * @param timestamp Timestamp associated with when the value became valid.
   */
  ProcessedSampledData(AstlValue value, SampleTimestamp timestamp) : value{value}, timestamp{timestamp} {}

  /** @brief The processed metric value. */
  AstlValue value;
  /** @brief Time the sample was generated (steady clock, microsecond resolution). */
  SampleTimestamp timestamp;

  /**
   * @brief Retrieve the stored value as a concrete type.
   * @tparam T Exact type contained in the underlying variant.
   * @return const reference to the stored value.
   * @note This does not perform type conversion; `T` must match exactly.
   */
  template <typename T>
  const auto &get() const {
    return std::get<T>(value.value);
  }
};

/* IProcessedSampleSink is an interface for anything that can receive processed sampled data.
 * This might include the Orchestrator, CollectorManager, or output writers, as well as test components.
 */
struct IProcessedSampleSink {
  virtual ~IProcessedSampleSink() = default;

  IProcessedSampleSink()                                        = default;
  IProcessedSampleSink(const IProcessedSampleSink &)            = default;
  IProcessedSampleSink &operator=(IProcessedSampleSink const &) = default;
  IProcessedSampleSink(IProcessedSampleSink &&)                 = default;
  IProcessedSampleSink &operator=(IProcessedSampleSink &&)      = default;

  /**
   * @brief Deliver one or more processed samples for a metric on a specific target.
   * @param target Target the samples originate from (non-null when metric is target-scoped).
   * @param metric Metric instance that produced the samples.
   * @param samples Span of processed samples to consume; lifetime extends only for the duration of this call.
   * @return ASTL_STATUS_SUCCESS on success or an error status indicating the sink failed to consume samples.
   */
  virtual auto SinkProcessedSamples(const ITarget *target, const IMetric *metric,
                                    std::span<const ProcessedSampledData> samples) -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_PROCESSED_SAMPLE_SINK_HPP_
