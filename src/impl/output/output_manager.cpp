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

#include "output_manager.hpp"

#include <new>  // std::bad_alloc

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "buffer_output.hpp"
#include "common/astl_defines.hpp"

namespace astl {

astl_status_code OutputManager::CreateBufferOutput(std::span<astl_metric_sample_t> samples_buffer,
                                                   uint32_t*                       buffer_sample_count) {
  if (samples_buffer.empty() || buffer_sample_count == nullptr) {
    ASTL_LOG_ERROR("CreateBufferOutput: invalid arguments (empty span or null count pointer)");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  if (_buffer_output) {
    ASTL_LOG_INFO("CreateBufferOutput: replacing existing buffer output instance");
  }
  try {
    _buffer_output = std::make_unique<BufferOutput>(samples_buffer, buffer_sample_count);

  } catch (const std::bad_alloc& e) {
    ASTL_LOG_ERROR("CreateBufferOutput: allocation failed (bad_alloc): {}", e.what());
    return ASTL_STATUS_OUT_OF_MEMORY;
  } catch (...) {  // NOLINT(bugprone-empty-catch)
    ASTL_LOG_ERROR("CreateBufferOutput: unexpected exception during buffer output creation");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

astl_status_code OutputManager::DestroyBufferOutput() {
  if (_buffer_output) {
    ASTL_LOG_INFO("DestroyBufferOutput: releasing buffer output instance");
  }
  _buffer_output.reset();
  return ASTL_STATUS_SUCCESS;
}

astl_status_code OutputManager::OutputProcessedSamplesToBuffer(const ProcessedSamplesMap& processed_samples,
                                                               const ITarget* target, const IMetric* metric) {
  if (!_buffer_output) {
    ASTL_LOG_ERROR("OutputProcessedSamplesToBuffer: Buffer output not initialized");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (target == nullptr || metric == nullptr) {
    ASTL_LOG_ERROR("OutputProcessedSamplesToBuffer: Target or Metric is null");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto target_iter = processed_samples.find(target);
  if (target_iter == processed_samples.end()) {
    ASTL_LOG_ERROR("OutputProcessedSamplesToBuffer: No processed samples for target '{}'", target->Name());
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto metric_iter = target_iter->second.find(metric);
  if (metric_iter == target_iter->second.end()) {
    ASTL_LOG_ERROR("OutputProcessedSamplesToBuffer: No processed samples for metric '{}'", metric->Name());
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  const auto& samples = metric_iter->second;
  if (samples.empty()) {
    ASTL_LOG_ERROR("OutputProcessedSamplesToBuffer: No processed samples available");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  std::span<const ProcessedSampledData> samples_span(samples);
  // Forward the call to the buffer output
  return _buffer_output->WriteProcessedSamples(samples_span);
}

astl_status_code OutputManager::OutputProcessedSamples(const ProcessedSamplesMap& processed_samples,
                                                       OutputType output_type, const ITarget* target,
                                                       const IMetric* metric) {
  switch (output_type) {
    case OutputType::BUFFER: {
      // Single-dispatch write; caller manages lifecycle of buffer output.
      return OutputProcessedSamplesToBuffer(processed_samples, target, metric);
    }
    case OutputType::UNKNOWN:
    default: {
      ASTL_LOG_ERROR("OutputProcessedSamples: Unknown output type");
      return ASTL_STATUS_BAD_ARGUMENT;
    }
  }
  // Unreachable, but placate some compilers/readability tools.
  return ASTL_STATUS_INTERNAL_ERROR;
}

}  // namespace astl
