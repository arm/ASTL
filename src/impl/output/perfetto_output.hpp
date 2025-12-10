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
 * @file perfetto_output.hpp
 * @brief Writes processed metric samples to a Chrome / Perfetto JSON trace file.
 *
 * Core behaviors:
 *  - Numeric samples are emitted as Perfetto counter events (ph = "C").
 *  - Non-numeric samples (e.g. std::string) are emitted as instant events (ph = "I", scope thread).
 *  - Each target is assigned a stable pid. Each metric under that target is assigned a distinct tid
 *    (thread id) so metrics on the same target appear on separate tracks within the target process.
 *    pid assignment starts at 1; tids for a given pid start at 1 and increment as new metrics appear.
 *  - Environment variable `ASTL_OUTPUT_PERFETTO` selects the output file path; if unset or the file
 *    cannot be opened the output stays disabled (Ready() == false).
 *  - Timestamps are monotonic steady-clock microseconds (not wall time). For correlation across
 *    systems an external mapping to wall time is required.
 *  - Name sanitization replaces whitespace and '"' with '_' for metric and target names; string
 *    sample payloads are escaped using EscapeJsonString() to keep JSON valid.
 *  - The file is a single JSON array: header '[' written once, events appended with commas, footer
 *    ']' written at destruction for well-formed output even if no samples were produced.
 *
 * Schema examples:
 *  Counter (numeric):
 *    {"ph":"C","cat":"ASTL","name":"TargetX.MetricA","ts":1234,"pid":1,"tid":1,
 *     "args":{"target":"TargetX","metric":"MetricA","value":3.14}}
 *  Instant (string):
 *    {"ph":"I","cat":"ASTL","name":"TargetX.StateMetric","ts":1235,"pid":1,"tid":1,
 *     "s":"t","args":{"target":"TargetX","metric":"StateMetric","value":"EnteringState"}}
 */
#ifndef PERFETTO_OUTPUT_HPP_
#define PERFETTO_OUTPUT_HPP_

#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>

#include "astl/astl_errors.h"
#include "common/astl_defines.hpp"  // ProcessedSamplesMap, ITarget, IMetric
#include "output/i_output.hpp"      // Base interface

namespace astl {

// See detailed schema in file header comment above.
class PerfettoOutput : public IOutput {
 public:
  explicit PerfettoOutput(std::filesystem::path path);
  PerfettoOutput(const PerfettoOutput&)                    = delete;
  auto operator=(const PerfettoOutput&) -> PerfettoOutput& = delete;
  PerfettoOutput(PerfettoOutput&&)                         = delete;
  auto operator=(PerfettoOutput&&) -> PerfettoOutput&      = delete;
  ~PerfettoOutput() override;  // override base virtual destructor

  using IOutput::WriteProcessedSamples;  // bring other overloads into scope to avoid -Woverloaded-virtual

  /**
   * @brief Whether the output is available and header initialized.
   * @return true if the stream is open and header '[' has been written.
   */
  auto Ready() const -> bool { return _trace_stream.is_open() && _opened_json; }

  /**
   * @brief Serialize processed samples as Perfetto events.
   *
   * For each target/metric/sample triple:
   *  - If sample value is arithmetic -> emit counter event (ph:"C").
   *  - Else -> emit instant event (ph:"I") with thread scope (s:"t").
   *
   * @param processed Nested map Target* -> Metric* -> vector<ProcessedSampledData>
   * @return astl_status_code Success or internal error if output not ready.
   */
  auto WriteProcessedSamples(const ProcessedSamplesMap& samples)
      -> astl_status_code;  // non-const; mutates internal state

 private:
  std::filesystem::path _path;
  std::ofstream         _trace_stream;  // JSON trace output stream
  bool                  _opened_json{false};
  bool                  _first_event{true};
  // Mapping of targets to assigned pid; and per-target mapping of metrics to tids.
  std::unordered_map<const ITarget*, int>                                     _pid_map;  // unique process id per target
  int                                                                         _next_pid{1};  // next pid to assign
  std::unordered_map<const ITarget*, std::unordered_map<const IMetric*, int>> _tid_map;      // per-target metric->tid
  std::unordered_map<const ITarget*, int> _next_tid_map;  // per-target next tid counter

  auto WriteHeader() -> void;
  auto WriteFooter() -> void;
  // Sanitize metric/target names for Perfetto (whitespace & quotes -> '_').
  static auto Sanitize(std::string_view input) -> std::string;
  // Escape a string sample value for JSON inclusion (quotes, backslashes, control chars).
  static auto EscapeJsonString(std::string_view input) -> std::string;
  // Assign or lookup a stable pid for a target.
  auto GetPid(const ITarget* target) -> int;
  // Assign or lookup a stable tid for a (target, metric) pair.
  auto GetTid(const ITarget* target, const IMetric* metric) -> int;
  // Derive a richer event category from units (first matching rule wins):
  //  ASTL_UNITS_WATTS      -> "Power"
  //  ASTL_UNITS_JOULES     -> "Energy"
  //  ASTL_UNITS_CELSIUS    -> "Temperature"
  //  ASTL_UNITS_MHERTZ     -> "Frequency"
  //  ASTL_UNITS_VOLTS      -> "Voltage"
  //  ASTL_UNITS_AMPS       -> "Current"
  //  ASTL_UNITS_BYTES      -> "Bytes"
  //  ASTL_UNITS_MBYTESPERSEC -> "Bandwidth"
  //  ASTL_UNITS_TICKS      -> "Ticks"
  //  ASTL_UNITS_SECONDS    -> "Time"
  //  (string sample with non-quantitative units) -> "State"
  //  fallback -> ""
  static auto DetermineCategory(astl_units_t units, bool is_string_sample) -> std::string;
};

}  // namespace astl

#endif  // PERFETTO_OUTPUT_HPP_
