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

/**
 * @file i_output.hpp
 * @brief Base interface for concrete output sinks.
 */
#ifndef I_OUTPUT_HPP_
#define I_OUTPUT_HPP_

#include "common/astl_defines.hpp"  // ProcessedSamplesMap alias
#include "common/i_processed_sample_sink.hpp"

namespace astl {

/**
 * @brief Abstract output sink for processed samples.
 *
 * Design:
 *  - Interface returns `astl_status_code` instead of throwing.
 *  - Implementations may be stateful (e.g. owning a file descriptor) but should strive to keep
 *    writes cheap; expensive formatting should be performed upstream when possible.
 *  - Thread-safety: NOT guaranteed. Callers must externally synchronize concurrent invocations.
 */
struct IOutput {
  virtual ~IOutput() = default;

  IOutput()                          = default;
  IOutput(const IOutput&)            = default;
  IOutput& operator=(const IOutput&) = default;
  IOutput(IOutput&&)                 = default;
  IOutput& operator=(IOutput&&)      = default;

  /**
   * @brief Write a contiguous span of processed samples to the underlying destination.
   *
   * Contract:
   *  - The span represents a single logical flush already grouped by upstream code.
   *  - Implementations MUST NOT retain references or pointers to elements after the call unless
   *    explicitly documented (i.e. pure pass-through / formatting only).
   *  - Ordering is preserved as provided.
   *  - Caller is responsible for ensuring thread-safety if the implementation does not document
   *    internal synchronization.
   *
   * Error Handling: Implementations should perform best-effort writes; partial writes are only
   * allowed if explicitly documented by the concrete implementation.
   *
   * @param samples Span of processed sampled data in chronological order.
   * @retval ASTL_STATUS_SUCCESS Entire span written.
   * @retval ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED Written successfully with unused downstream capacity (buffer outputs
   * only).
   * @retval ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL Destination buffer could not hold all samples (no partial write
   * unless documented).
   * @retval ASTL_STATUS_INTERNAL_ERROR Implementation-specific failure (e.g. null internal pointer, IO error).
   * @retval ASTL_STATUS_NOT_IMPLEMENTED Default base implementation (when not overridden).
   */
  [[nodiscard]] virtual auto WriteProcessedSamples(const std::span<const ProcessedSampledData>& samples)
      const  // NOLINT(readability-convert-member-function-to-static)
      -> astl_status_code {
    (void)samples;  // unused default implementation
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }

  /**
   * @brief Write all processed samples contained in a nested target/metric map.
   *
   * Map Shape:
   *  outer key: const ITarget* (nullptr entries SHOULD be ignored by implementations)
   *  inner key: const IMetric* (nullptr entries SHOULD be ignored)
   *  value:     std::vector<ProcessedSampledData> (may be empty; empty vectors SHOULD be skipped)
   *
   * Contract:
   *  - Lifetime of pointers (ITarget / IMetric) and vector contents is controlled by caller and
   *    remains valid for the duration of this call only.
   *  - Implementations MUST NOT modify the map or its contents.
   *  - Ordering of samples inside each vector is assumed chronological and must be preserved.
   *  - Implementations may choose to emit no output if every vector is empty or every key invalid.
   *
   * Thread-safety: Same as span overload—caller must serialize concurrent invocations unless the
   * implementation documents internal synchronization.
   *
   * @param processed Nested map Target* -> (Metric* -> vector<ProcessedSampledData>).
   * @retval ASTL_STATUS_SUCCESS All applicable samples written.
   * @retval ASTL_STATUS_INTERNAL_ERROR Implementation-specific failure (e.g. underlying IO error).
   * @retval ASTL_STATUS_NOT_IMPLEMENTED Default base implementation (when not overridden).
   */
  [[nodiscard]] virtual auto WriteProcessedSamples(
      const ProcessedSamplesMap& processed) const  // NOLINT(readability-convert-member-function-to-static)
      -> astl_status_code {
    (void)processed;  // unused default implementation
    return ASTL_STATUS_NOT_IMPLEMENTED;
  }
};

}  // namespace astl

#endif  // I_OUTPUT_HPP_
