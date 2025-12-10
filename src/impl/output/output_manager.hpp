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
 * @file output_manager.hpp
 * @brief Concrete implementation of `IOutputManager` that coordinates output writers.
 *
 * Currently supports a single output type (in-memory buffer). Future extensions may add file,
 * streaming, or telemetry transport outputs without altering the orchestrator layer.
 */
#ifndef OUTPUT_MANAGER_HPP_
#define OUTPUT_MANAGER_HPP_

#include <span>

#include "astl/astl.h"
#include "buffer_output.hpp"
#include "common/astl_defines.hpp"
#include "i_output_manager.hpp"
#include "interval_csv_output.hpp"
#include "perfetto_output.hpp"
#include "target.hpp"

namespace astl {

class BufferOutput;    // fwd
class PerfettoOutput;  // fwd

/**
 * @brief Manages creation and dispatch to concrete `IOutput` instances.
 *
 * Design Notes:
 *  - Non-copyable to preserve unique ownership of underlying outputs.
 *  - Move operations allowed so higher-level components can transfer ownership.
 *  - Not inherently thread-safe; callers should serialize concurrent writes if needed.
 */
class OutputManager : public IOutputManager {
 public:
  /*
   * @brief Construct the Output manager
   *
   */
  explicit OutputManager() : _buffer_output(nullptr), _perfetto_output(nullptr) {}

  // OutputManager owns its IOutput instances, so it can be moved, but not copied
  OutputManager(OutputManager const&)            = delete;
  OutputManager& operator=(OutputManager const&) = delete;
  OutputManager(OutputManager&&)                 = default;
  OutputManager& operator=(OutputManager&&)      = default;

  ~OutputManager() override = default;

  /**
   * @brief Dispatch processed samples to the selected output type / filter.
   * See `IOutputManager::OutputProcessedSamples` for detailed contract & error codes.
   */
  [[nodiscard]] auto OutputProcessedSamples(const ProcessedSamplesMap& processed_samples, OutputType output_type,
                                            const ITarget* target, const IMetric* metric) -> astl_status_code override;

  /**
   * @copydoc IOutputManager::CreateBufferOutput
   */
  /**
   * @brief Create (or replace) the buffer output.
   *
   * If a buffer output already exists it is replaced. Future change: consider returning
   * ASTL_STATUS_ALREADY_INITIALIZED to force explicit destruction.
   */
  [[nodiscard]] auto CreateBufferOutput(std::span<astl_metric_sample_t> samples_buffer, uint32_t* buffer_sample_count)
      -> astl_status_code override;

  /**
   * @copydoc IOutputManager::DestroyBufferOutput
   */
  auto DestroyBufferOutput() -> astl_status_code override;

 private:
  /**
   * @brief Write processed samples to the registered buffer output.
   *
   * Applies filtering by `target` and/or `metric` (when non-null) before forwarding
   * samples to the underlying `IOutput` implementation. If no buffer output has been created
   * `ASTL_STATUS_OUTPUT_NOT_INITIALIZED` is returned. Any error returned by the buffer output
   * write path is propagated unchanged.
   *
   * @param processed_samples Map of metrics -> (target -> span of processed samples).
   * @param target single target filter.
   * @param metric single metric filter.
   * @return ASTL_STATUS_SUCCESS on success, `ASTL_STATUS_OUTPUT_NOT_INITIALIZED` if no buffer output
   *         exists, or a status code returned by the buffer output on write failure.
   */
  [[nodiscard]] auto OutputProcessedSamplesToBuffer(const ProcessedSamplesMap& processed_samples, const ITarget* target,
                                                    const IMetric* metric) -> astl_status_code;

  std::unique_ptr<BufferOutput>      _buffer_output;
  std::unique_ptr<PerfettoOutput>    _perfetto_output;      // lazy init
  std::unique_ptr<IntervalCsvOutput> _interval_csv_output;  // lazy init

  // Ensure the perfetto writer is constructed if enabled.
  auto EnsurePerfettoOutput() -> astl_status_code;

  // Ensure the interval CSV writer is constructed if enabled.
  auto EnsureIntervalCsvOutput() -> astl_status_code;
};

}  // namespace astl

#endif  // OUTPUT_MANAGER_HPP_
