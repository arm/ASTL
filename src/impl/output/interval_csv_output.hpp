// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file interval_csv_output.hpp
 * @brief Writes processed samples to a time-interval CSV file.
 *
 * Deferred Emission:
 *  - Similar to Perfetto output: only produced at StopCollection when
 *    ASTL_OUTPUT_INTERVAL_CSV env var is set to a path. Lifetime is a single write.
 *
 * Format (ATX-compatible interval sections):
 *  For each metric/target pair encountered:
 *    <metric_name> on <target_name>
 *
 *    timestamp_us,value
 *    <one row per sample for that metric-target series>
 *
 *  Blank line separates sections. Empty collection yields collection info only.
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
 *  @brief Emits processed interval samples in an ATX-compatible CSV format.
 *  @details Usage is deferred: file opens on construction but rows are only
 *           written once via WriteProcessedSamples (called from orchestrator
 *           stop path when ASTL_OUTPUT_INTERVAL_CSV is set). Each metric-target
 *           section has an info row (`metric on target`), a header row
 *           (`timestamp_us,value`), then sample rows. Sections are ordered by
 *           metric then target; blank line separates sections. Empty set
 *           produces collection info only. Thread-safety: not thread-safe.
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
