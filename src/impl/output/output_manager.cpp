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

#include <cstdlib>  // std::getenv
#include <filesystem>
#include <fstream>
#include <map>
#include <new>  // std::bad_alloc
#include <span>
#include <tuple>
#include <variant>

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl_utils.hpp"
#include "buffer_output.hpp"
#include "common/astl_defines.hpp"
#include "common/astl_value.hpp"
#include "interval_csv_output.hpp"
#include "perfetto_output.hpp"
#include "summarizer.hpp"
#include "summary_csv_output.hpp"

namespace astl {

auto OutputManager::CreateBufferOutput(std::span<astl_metric_sample_t> samples_buffer, uint32_t* buffer_sample_count)
    -> astl_status_code {
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
  } catch (...) {
    // Catch-all: we intentionally map any other exception type to an internal error.
    // Rationale: OutputManager must not allow exceptions to escape the C boundary; logging preserves context.
    ASTL_LOG_ERROR("CreateBufferOutput: unexpected exception during buffer output creation");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

auto OutputManager::DestroyBufferOutput() -> astl_status_code {
  if (_buffer_output) {
    ASTL_LOG_INFO("DestroyBufferOutput: releasing buffer output instance");
  }
  _buffer_output.reset();
  return ASTL_STATUS_SUCCESS;
}

auto OutputManager::EnsurePerfettoOutput() -> astl_status_code {
  if (_perfetto_output && _perfetto_output->Ready()) {
    return ASTL_STATUS_SUCCESS;
  }

  std::string perfetto_path = astl::GetEnvVar("ASTL_OUTPUT_PERFETTO");
  if (perfetto_path.empty()) {
    ASTL_LOG_ERROR("Perfetto output requested but ASTL_OUTPUT_PERFETTO is not set or empty");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  try {
    _perfetto_output = std::make_unique<PerfettoOutput>(std::filesystem::path(perfetto_path));
  } catch (const std::exception& exception) {
    ASTL_LOG_ERROR("EnsurePerfettoOutput: exception while creating writer: {}", exception.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_perfetto_output->Ready()) {
    ASTL_LOG_ERROR("EnsurePerfettoOutput: writer not ready after creation (path='{}')", perfetto_path);
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

auto OutputManager::EnsureIntervalCsvOutput() -> astl_status_code {
  if (_interval_csv_output && _interval_csv_output->Ready()) {
    return ASTL_STATUS_SUCCESS;
  }
  // TODO(ASTL-208): centralize env var keys as constexprs to avoid duplication.
  std::string csv_path = astl::GetEnvVar("ASTL_OUTPUT_INTERVAL_CSV");
  if (csv_path.empty()) {
    ASTL_LOG_ERROR("Interval CSV output requested but ASTL_OUTPUT_INTERVAL_CSV is not set or empty");
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  try {
    _interval_csv_output = std::make_unique<IntervalCsvOutput>(std::filesystem::path(csv_path));
  } catch (const std::exception& ex) {
    ASTL_LOG_ERROR("EnsureIntervalCsvOutput: exception while creating writer: {}", ex.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_interval_csv_output->Ready()) {
    ASTL_LOG_ERROR("EnsureIntervalCsvOutput: writer not ready after creation (path='{}')", csv_path);
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

auto OutputManager::OutputProcessedSamplesToBuffer(const ProcessedSamplesMap& processed_samples, const ITarget* target,
                                                   const IMetric* metric) -> astl_status_code {
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

auto OutputManager::OutputProcessedSamples(const ProcessedSamplesMap& processed_samples, OutputType output_type,
                                           const ITarget* target, const IMetric* metric) -> astl_status_code {
  switch (output_type) {
    case OutputType::BUFFER: {
      // Single-dispatch write; caller manages lifecycle of buffer output.
      return OutputProcessedSamplesToBuffer(processed_samples, target, metric);
    }
    case OutputType::PERFETTO: {
      auto status_code = EnsurePerfettoOutput();
      if (status_code != ASTL_STATUS_SUCCESS) {
        return status_code;
      }
      // Ignore target/metric parameters; write all samples.
      return _perfetto_output->WriteProcessedSamples(processed_samples);
    }
    case OutputType::INTERVAL_CSV: {
      auto status_code = EnsureIntervalCsvOutput();
      if (status_code != ASTL_STATUS_SUCCESS) {
        return status_code;
      }
      return _interval_csv_output->WriteProcessedSamples(processed_samples);
    }
    case OutputType::SUMMARY_CSV: {
      // For SUMMARY_CSV output, process ALL metrics on ALL targets, grouped by metric name.
      // Emission is opt-in via ASTL_OUTPUT_SUMMARY_CSV.
      const char* csv_file_path = std::getenv("ASTL_OUTPUT_SUMMARY_CSV");
      if (csv_file_path == nullptr || *csv_file_path == '\0') {
        ASTL_LOG_ERROR("OutputProcessedSamples: ASTL_OUTPUT_SUMMARY_CSV environment variable not set or empty");
        return ASTL_STATUS_BAD_ARGUMENT;
      }
      SummaryCsvOutput summary_csv_output(csv_file_path);
      if (!summary_csv_output.Ready()) {
        ASTL_LOG_ERROR("OutputProcessedSamples: Failed to initialize summary CSV output (path='{}')", csv_file_path);
        return ASTL_STATUS_INTERNAL_ERROR;
      }
      return summary_csv_output.WriteProcessedSamples(processed_samples);
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
