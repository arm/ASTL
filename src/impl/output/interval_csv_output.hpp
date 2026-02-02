/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
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
 * @file interval_csv_output.hpp
 * @brief Writes processed samples to a time-interval CSV file.
 *
 * Deferred Emission:
 *  - Similar to Perfetto output: only produced at StopCollection when
 *    ASTL_OUTPUT_INTERVAL_CSV env var is set to a path. Lifetime is a single write.
 *
 * Format (grouped by metric name, hybrid rows):
 *  For each unique metric name encountered across all targets:
 *    metric_name,metric_description        (info row; description is first non-empty encountered)
 *    timestamp_us,target,metric,value      (per-metric header line)
 *    <one row per sample for that metric across all targets>
 *        where each sample row repeats the metric name for easier downstream filtering
 *  Blank line separates metric groups. Empty collection yields empty file.
 */
#ifndef INTERVAL_CSV_OUTPUT_HPP_
#define INTERVAL_CSV_OUTPUT_HPP_

#include <filesystem>
#include <fstream>

#include "common/astl_defines.hpp"
#include "i_output.hpp"

/** @defgroup output_writers Output Writers
 *  @brief Components converting processed samples to external representations.
 *  @details Current writers: BufferOutput (in-memory), PerfettoOutput (JSON trace), IntervalCsvOutput (grouped CSV).
 */
namespace astl {

/** @class IntervalCsvOutput
 *  @ingroup output_writers
 *  @brief Emits processed interval samples in a grouped CSV format.
 *  @details Usage is deferred: file opens on construction but rows are only
 *           written once via WriteProcessedSamples (called from orchestrator
 *           stop path when ASTL_OUTPUT_INTERVAL_CSV is set). Each metric group has:
 *           an info row (name, description), a header row
 *           (timestamp_us,target,metric,value), then sample rows repeating the
 *           metric name. Groups ordered alphabetically; blank line separates
 *           groups. Empty set produces empty file. Thread-safety: not thread-safe.
 */
class IntervalCsvOutput : public IOutput {  // NOLINT(cppcoreguidelines-special-member-functions)
 public:
  /** @brief Construct writer with destination path (parent dirs best-effort created). */
  explicit IntervalCsvOutput(std::filesystem::path path);
  ~IntervalCsvOutput() override = default;

  /** @brief Indicates file stream is ready for writing (true if open). */
  [[nodiscard]] auto Ready() const -> bool { return _output_stream.is_open(); }

  /** @brief Write all processed samples.
   *  @param processed Aggregated map: target* -> (metric* -> vector of processed samples).
   *  @return ASTL_STATUS_SUCCESS on success; ASTL_STATUS_INTERNAL_ERROR if stream not ready.
   */
  [[nodiscard]] auto WriteProcessedSamples(const ProcessedSamplesMap& processed) -> astl_status_code override;

 private:
  std::filesystem::path _path;           //!< Destination path
  std::ofstream         _output_stream;  //!< Output file stream (opened on construction)
};

}  // namespace astl

#endif  // INTERVAL_CSV_OUTPUT_HPP_
