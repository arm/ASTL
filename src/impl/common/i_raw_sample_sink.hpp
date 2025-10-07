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

#ifndef I_RAW_SAMPLE_SINK_HPP_
#define I_RAW_SAMPLE_SINK_HPP_

#include <chrono>
#include <span>
#include <utility>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "counter.hpp"
#include "operation/operation.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Represents a single raw (unprocessed) metric sample produced by a collector operation.
 *
 * Raw samples are produced directly from hardware/firmware (e.g. SCMI reads) before any metric-level
 * interpretation such as delta, rate, or aggregation is applied. Each raw sample is tagged with the
 * `OperationId` that initiated its collection so the metric layer can dispatch it to the correct
 * `IMetric` instance.
 *
 * Thread-safety: Once constructed the object is immutable and can be shared safely between threads
 * if the underlying `AstlValue` variant alternative is trivially copyable (the typical case).
 */
struct RawSampledData {
  RawSampledData() = delete;

  /**
   * @brief Construct a raw sample with current timestamp.
   * @param operation_id Identifier of the originating `Operation`.
   * @param value The captured raw value.
   */
  explicit RawSampledData(OperationId operation_id, AstlValue value)
      : operation_id{operation_id},
        value{value},
        timestamp{std::chrono::time_point_cast<SampleTimestamp::duration>(std::chrono::steady_clock::now())} {}

  /**
   * @brief Construct a raw sample with an explicit timestamp.
   * @param operation_id Identifier of the originating `Operation`.
   * @param value The captured raw value.
   * @param timestamp Time the value was read from the source.
   */
  RawSampledData(OperationId operation_id, AstlValue value, SampleTimestamp timestamp)
      : operation_id{operation_id}, value{value}, timestamp{timestamp} {}

  /** @brief Identifier of the operation that produced this sample. */
  OperationId operation_id{kOperationIdInvalid};
  /** @brief Raw value captured (pre-metric processing). */
  AstlValue value;
  /** @brief Timestamp of capture (steady clock, microsecond resolution). */
  SampleTimestamp timestamp;

  /**
   * @brief Retrieve the stored raw value as a concrete type.
   * @tparam T Exact variant alternative expected.
   * @return Reference to stored value.
   */
  template <typename T>
  const auto &get() const {
    return std::get<T>(value.value);
  }
};

/* IRawSampleSink is an interface for anything that can receive raw sampled data.
 * This might include the Orchestrator, CollectorManager, or output writers, as well as test components.
 */
struct IRawSampleSink {
  virtual ~IRawSampleSink() = default;

  IRawSampleSink()                                  = default;
  IRawSampleSink(const IRawSampleSink &)            = default;
  IRawSampleSink &operator=(IRawSampleSink const &) = default;
  IRawSampleSink(IRawSampleSink &&)                 = default;
  IRawSampleSink &operator=(IRawSampleSink &&)      = default;

  /**
   * @brief Deliver one or more raw samples collected for a target.
   * @param target Target on which samples were collected (may be nullptr for global metrics).
   * @param raw_samples Span of raw samples; valid only for the duration of the call.
   * @return ASTL_STATUS_SUCCESS on success or an error status if the sink cannot consume the data.
   */
  virtual auto SinkRawSamples(const ITarget *target, std::span<RawSampledData> raw_samples) -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_RAW_SAMPLE_SINK_HPP_
