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

#include "summary_output.hpp"

#include <string>

#include "astl_logger.hpp"  // ASTL_LOG_*

namespace astl {

SummaryOutput::SummaryOutput(std::vector<std::unique_ptr<ISummarizer>> summarizers)
    : summarizers_(std::move(summarizers)) {}

auto SummaryOutput::WriteProcessedSamples(const ProcessedSamplesMap& samples) const -> astl_status_code {
  std::vector<std::tuple<const ITarget*, const IMetric*, SummaryResult>> summaries;

  // Compute summaries for each target-metric combination
  for (const auto& [target, target_metrics] : samples) {
    for (const auto& [metric, sample_data] : target_metrics) {
      auto metric_summaries = ComputeSummariesForMetric(target, metric, sample_data);
      for (const auto& summary_result : metric_summaries) {
        summaries.emplace_back(target, metric, summary_result);
      }
    }
  }

  return WriteSummaries(summaries);
}

auto SummaryOutput::ComputeSummariesForMetric(  // NOLINT(readability-convert-member-functions-to-static)
    const ITarget* target, const IMetric* metric, std::span<const ProcessedSampledData> sample_data) const
    -> std::vector<SummaryResult> {
  std::string                metric_name = metric->Name();
  std::vector<SummaryResult> results;

  // Get metric properties to check if summarizers can handle this metric
  astl_metric_properties_t metric_props{};
  metric_props._size = sizeof(astl_metric_properties_t);

  auto props_result = metric->GetProperties(&metric_props);
  if (props_result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_WARNING("Failed to get metric properties for '{}' from target '{}'", metric_name, target->Name());
    return results;
  }

  if (sample_data.empty()) {
    ASTL_LOG_DEBUG("No samples for metric '{}' from target '{}'", metric_name, target->Name());
    return results;
  }

  // Try each summarizer that can handle this metric
  for (const auto& summarizer : summarizers_) {
    if (!summarizer->IsSupported(metric_props._value_type, metric_props._metric_type)) {
      ASTL_LOG_DEBUG("Skipping summarizer '{}' for metric '{}' from target '{}' - not supported",
                     summarizer->GetSummaryType(), metric_name, target->Name());
      continue;
    }

    auto summary_result = summarizer->Summarize(sample_data);
    if (!summary_result.has_value()) {
      ASTL_LOG_WARNING("Failed to summarize metric '{}' from target '{}' with summarizer '{}'", metric_name,
                       target->Name(), summarizer->GetSummaryType());
      continue;
    }

    results.push_back(summary_result.value());
  }

  if (results.empty()) {
    ASTL_LOG_WARNING("No summarizers could process metric '{}' from target '{}'", metric_name, target->Name());
  }

  return results;
}

}  // namespace astl
