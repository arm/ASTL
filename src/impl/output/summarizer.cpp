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

#include "summarizer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include "../common/i_processed_sample_sink.hpp"
#include "astl_logger.hpp"
#include "astl_value.hpp"

namespace astl {

// Helper function to check if a value type is arithmetic
static constexpr bool IsArithmeticValueType(astl_value_type_t value_type) {
  return value_type == ASTL_VALUE_UINT8 || value_type == ASTL_VALUE_UINT16 || value_type == ASTL_VALUE_UINT32 ||
         value_type == ASTL_VALUE_UINT64 || value_type == ASTL_VALUE_FLOAT32 || value_type == ASTL_VALUE_FLOAT64;
}

// MinMaxAvgSummarizer Implementation
std::expected<SummaryResult, astl_status_code> MinMaxAvgSummarizer::Summarize(
    std::span<const ProcessedSampledData> samples) const {
  if (samples.empty()) {
    ASTL_LOG_TRACE("MinMaxAvgSummarizer: No samples to summarize");
    return MinMaxAvgSummary{std::nullopt, std::nullopt, std::nullopt, 0};
  }

  MinMaxAvgSummary summary{};
  summary.count = samples.size();

  // Find first arithmetic sample to initialize min/max
  auto arithmetic_sample_it = std::find_if(
      samples.begin(), samples.end(), [](const ProcessedSampledData& sample) { return sample.value.IsArithmetic(); });

  if (arithmetic_sample_it == samples.end()) {
    ASTL_LOG_TRACE("MinMaxAvgSummarizer: No arithmetic samples found");
    return summary;
  }

  // Initialize min/max with first arithmetic value
  summary.min = arithmetic_sample_it->value;
  summary.max = arithmetic_sample_it->value;

  // Get type for creating a zero value for sum
  auto [union_val, value_type] = arithmetic_sample_it->value.ToAstlUnionValue();
  auto sum_result              = AstlValue::FromUnionPromoting(value_type);
  if (!sum_result) {
    ASTL_LOG_ERROR("MinMaxAvgSummarizer: Failed to create initial sum");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto        sum              = sum_result.value();
  std::size_t arithmetic_count = 0;

  for (const auto& sample : samples) {
    if (!sample.value.IsArithmetic()) {
      continue;
    }

    // Update min/max
    summary.min = std::min(sample.value, summary.min.value());
    summary.max = std::max(sample.value, summary.max.value());

    // Add to sum
    auto add_result = AstlValue::Add(sum, sample.value);
    if (!add_result) {
      ASTL_LOG_ERROR("MinMaxAvgSummarizer: Failed to add sample to sum");
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    sum = add_result.value();
    arithmetic_count++;
  }

  if (arithmetic_count == 0) {
    ASTL_LOG_DEBUG("MinMaxAvgSummarizer: No arithmetic samples to calculate average");
    return summary;
  }
  // Calculate average
  auto avg_result = AstlValue::Divide(sum, static_cast<double>(arithmetic_count));
  if (avg_result) {
    // Extract the double value and round to 2 decimal places
    double avg_value   = std::get<double>(avg_result.value().value);
    double rounded_avg = std::round(avg_value * 100.0) / 100.0;
    summary.avg        = AstlValue{rounded_avg};
  } else {
    ASTL_LOG_ERROR("MinMaxAvgSummarizer: Failed to calculate average");
  }

  return summary;
}

bool MinMaxAvgSummarizer::IsSupported(astl_value_type_t value_type, astl_metric_type_t metric_type) const {
  return (metric_type == ASTL_METRIC_VALUE || metric_type == ASTL_METRIC_DELTA || metric_type == ASTL_METRIC_RATE) &&
         IsArithmeticValueType(value_type);
}

}  // namespace astl