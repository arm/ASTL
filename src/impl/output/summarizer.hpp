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

#ifndef ASTL_SUMMARIZER_HPP_
#define ASTL_SUMMARIZER_HPP_

#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "common/astl_value.hpp"

namespace astl {

// Forward declarations
struct ProcessedSampledData;

/**
 * @brief Generic summary result that can hold different types of summary data.
 */
using SummaryResult = std::variant<struct MinMaxAvgSummary>;

/**
 * @brief Summary data for min/max/average statistics.
 */
struct MinMaxAvgSummary {
  std::optional<AstlValue> min;       ///< Minimum value
  std::optional<AstlValue> max;       ///< Maximum value
  std::optional<AstlValue> avg;       ///< Average value
  std::size_t              count{0};  ///< Number of samples processed
};

/**
 * @brief Base interface for all summarizer implementations.
 */
class ISummarizer {
 public:
  virtual ~ISummarizer() = default;

  // Abstract base class - delete copy operations and default move operations
  ISummarizer(const ISummarizer&)            = delete;
  ISummarizer& operator=(const ISummarizer&) = delete;
  ISummarizer(ISummarizer&&)                 = default;
  ISummarizer& operator=(ISummarizer&&)      = default;

  ISummarizer() = default;

  /**
   * @brief Generate a summary from processed sample data.
   *
   * @param samples Span of processed samples to summarize
   * @return Summary result or error status
   */
  virtual std::expected<SummaryResult, astl_status_code> Summarize(
      std::span<const ProcessedSampledData> samples) const = 0;

  /**
   * @brief Get the type of summary this summarizer produces.
   *
   * @return String identifier for the summary type
   */
  virtual std::string GetSummaryType() const = 0;

  /**
   * @brief Check if this summarizer supports the given value type and metric type combination.
   *
   * @param value_type The ASTL value type to check
   * @param metric_type The ASTL metric type to check
   * @return true if the summarizer supports this combination, false otherwise
   */
  virtual bool IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const = 0;
};

/**
 * @brief Summarizer for min/max/average statistics.
 *
 * Computes minimum, maximum, and average values from numeric samples.
 * Only works with arithmetic types (integers, floats).
 */
class MinMaxAvgSummarizer : public ISummarizer {
 public:
  std::expected<SummaryResult, astl_status_code> Summarize(
      std::span<const ProcessedSampledData> samples) const override;

  std::string GetSummaryType() const override { return "MinMaxAvg"; }

  bool IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const override;
};

}  // namespace astl

#endif  // ASTL_SUMMARIZER_HPP_