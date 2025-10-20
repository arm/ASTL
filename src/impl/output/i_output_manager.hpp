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
 * under the License
 ******************************************************************************/

/**
 * @file i_output_manager.hpp
 * @brief Interface for managing output writers that serialize processed metric samples.
 *
 * Responsibilities:
 *  - Own / create / destroy concrete `IOutput` implementations (e.g. in-memory buffer writer).
 *  - Dispatch processed samples (optionally filtered by target / metric) to a selected output type.
 *  - Provide a stable contract for higher-level orchestrator code without exposing concrete output classes.
 */
#ifndef I_OUTPUT_MANAGER_HPP_
#define I_OUTPUT_MANAGER_HPP_

#include <span>
#include <unordered_map>

#include "astl/astl.h"
#include "common/astl_defines.hpp"  // ProcessedSamplesMap, IMetric, ITarget

namespace astl {

/**
 * @brief Enumerates available output destinations.
 *
 * Additional output types (files, sockets, etc.) can be appended. Existing values must remain
 * stable for ABI compatibility.
 */
enum class OutputType { UNKNOWN, BUFFER, PERFETTO };

/**
 * @brief Abstract manager of one or more concrete `IOutput` instances.
 *
 * Implementations should be lightweight to construct and safe to destroy after all output
 * operations complete. Unless stated otherwise, methods are not guaranteed to be thread-safe; an
 * external synchronization strategy should be applied if outputs are written concurrently.
 */
struct IOutputManager {
  virtual ~IOutputManager() = default;

  IOutputManager()                                 = default;
  IOutputManager(const IOutputManager&)            = default;
  IOutputManager& operator=(const IOutputManager&) = default;
  IOutputManager(IOutputManager&&)                 = default;
  IOutputManager& operator=(IOutputManager&&)      = default;

  /**
   * @brief Create and register a buffer output writer.
   *
   * Any previously created buffer output should be destroyed (via `DestroyBufferOutput`) before
   * invoking this again; otherwise the implementation may return an error.
   *
   * Contract:
   *  - `samples_buffer` must reference a valid writable span whose lifetime exceeds any subsequent
   *    calls to `OutputProcessedSamples` that target `OutputType::BUFFER`.
   *  - `buffer_sample_count` must be non-null and will be set to the number of samples written
   *    after a flush. It is not modified on error.
   *
   * Error Codes:
   *  - ASTL_STATUS_BAD_ARGUMENT if inputs are invalid
   *  - ASTL_STATUS_OUT_OF_MEMORY if allocation for the output fails
   *  - ASTL_STATUS_INTERNAL_ERROR for unexpected failures
   */
  [[nodiscard]] virtual auto CreateBufferOutput(std::span<astl_metric_sample_t> samples_buffer,
                                                uint32_t* buffer_sample_count) -> astl_status_code = 0;

  /**
   * @brief Destroy and de-register the buffer output (idempotent).
   * @return ASTL_STATUS_SUCCESS (even if no buffer was registered) or error.
   */
  [[nodiscard]] virtual auto DestroyBufferOutput() -> astl_status_code = 0;

  /**
   * @brief Dispatch processed samples to the selected output type.
   * For `OutputType::BUFFER`, CreateOutputBuffer must be called to set up the OutputBuffer
   * writer instance before invoking this OutputProcessedSamples. Once the samples have been written to the buffer,
   * DestroyBufferOutput should be called to release the buffer output instance.
   *
   * Filters:
   *  - If `target` is non-null, only samples associated with that target are written.
   *  - If `metric` is non-null (and `target` is non-null), only samples for that metric on the
   *    target are written.
   * Filtering combinations outside this (e.g. metric without target) result in an error.
   *
   * @param processed_samples Map of collected processed samples (outer key: target, inner key: metric).
   * @param output_type Destination output selection.
   * @param target target filter.
   * @param metric metric filter (requires target).
   * @return ASTL_STATUS_SUCCESS, or:
   *   - ASTL_STATUS_BAD_ARGUMENT for invalid filter combinations
   *   - ASTL_STATUS_NO_DATA_COLLECTED if no matching samples exist
   *   - Implementation-specific error codes (e.g. capacity issues) from the underlying writer
   */
  [[nodiscard]] virtual auto OutputProcessedSamples(const ProcessedSamplesMap& processed_samples,
                                                    OutputType output_type, const ITarget* target,
                                                    const IMetric* metric) -> astl_status_code = 0;
};
}  // namespace astl

#endif  // I_OUTPUT_MANAGER_HPP_