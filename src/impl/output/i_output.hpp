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
   * @brief Write the provided processed samples to the underlying destination.
   *
   * Contract:
   *  - `samples` is an ordered contiguous span representing one logical flush.
   *  - Implementations must not retain references to elements beyond the call unless explicitly
   *    documented.
   *
   * Thread-safety: Caller must serialize if implementation not documented as safe.
   *
   * @param samples Contiguous collection of processed samples.
   * @retval ASTL_STATUS_SUCCESS Entire span written.
   * @retval ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED Written successfully with unused downstream capacity (if applicable).
   * @retval ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL Destination could not hold all samples (no partial writes
   * unless documented otherwise).
   * @retval ASTL_STATUS_INTERNAL_ERROR Implementation-specific failure (e.g. null internal pointer, IO error).
   */
  [[nodiscard]] virtual auto WriteProcessedSamples(const std::span<const ProcessedSampledData>& samples) const
      -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_OUTPUT_HPP_
