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
 * @file buffer_output.hpp
 * @brief Concrete `IOutput` implementation that serializes processed metric samples into a
 *        caller-provided buffer.
 *
 * Contract:
 *  - The caller owns the lifetime of the buffer span provided at construction.
 *  - `WriteProcessedSamples` writes sequentially from the start of the span; the total written
 *    count is reflected through `_buffer_sample_count`.
 *  - If the buffer is larger than needed, `ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED` is returned
 *    (success condition signaling unused capacity). If smaller, an error status (e.g.
 *    `ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL`) should be returned by the implementation.
 */
#ifndef BUFFER_OUTPUT_HPP_
#define BUFFER_OUTPUT_HPP_

#include <span>

#include "astl_logger.hpp"                     // logging macro
#include "common/i_processed_sample_sink.hpp"  // ProcessedSampledData
#include "output/i_output.hpp"

namespace astl {

/**
 * @brief In-memory buffer output writer.
 */
class BufferOutput : public IOutput {
 public:
  ~BufferOutput() override = default;

  /**
   * @brief Construct a buffer output writer.
   * @param samples_buffer Destination span. Must outlive this instance.
   * @param buffer_sample_count In/out pointer. Caller sets this to the declared capacity (number
   *                            of usable elements). On successful write it is replaced with the
   *                            number of samples written. Must be non-null.
   */
  explicit BufferOutput(std::span<astl_metric_sample_t> samples_buffer, uint32_t* buffer_sample_count)
      : _samples_buffer(samples_buffer), _buffer_sample_count(buffer_sample_count) {}

  BufferOutput(const BufferOutput&)            = default;
  BufferOutput& operator=(const BufferOutput&) = default;
  BufferOutput(BufferOutput&&)                 = default;
  BufferOutput& operator=(BufferOutput&&)      = default;

  /**
   * @brief Serialize the provided processed samples into the configured buffer.
   *
   * @param samples Ordered collection of processed samples for one or multiple metrics / targets.
   * @return ASTL_STATUS_SUCCESS on complete write; may also return
   *         ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED when buffer had excess capacity or an appropriate
   *         error if the buffer cannot hold all samples.
   */
  [[nodiscard]] astl_status_code WriteProcessedSamples(
      const std::span<const ProcessedSampledData>& samples) const override;

 private:
  // internal classes + enums
  std::span<astl_metric_sample_t> _samples_buffer;       //!< Destination span for metric samples
  uint32_t*                       _buffer_sample_count;  //!< In: capacity  Out: samples written
};

}  // namespace astl
#endif  // BUFFER_OUTPUT_HPP_
