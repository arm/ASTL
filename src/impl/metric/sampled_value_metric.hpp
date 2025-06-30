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

#ifndef SAMPLED_VALUE_METRIC_HPP_
#define SAMPLED_VALUE_METRIC_HPP_

#include <optional>

#include "astl/astl.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "raw_metric.hpp"

namespace astl {
/**
 * @brief Holds summary values for min, max, and average samples.
 * This structure is used to store computed statistics
 * TODO (https://jira.arm.com/browse/ASTL-99) : Add support for different summary types.
 * from sampled data in SampledValueMetric.
 */
struct MinMaxAvgSummaryData {
  std::optional<AstlValue> min;  ///< Minimum sample value seen
  std::optional<AstlValue> max;  ///< Maximum sample value seen
  std::optional<AstlValue> avg;  ///< Computed average of sample values
};

/**
 * @brief Base class interface for metrics that use sampled numerical data.
 *
 * SampledValueMetric provides common functionality such as accumulating
 * sample statistics and logging.
 */
class SampledValueMetric : public RawMetric {
 public:
  SampledValueMetric() = delete;
  /**
   * @brief Construct a SampledValueMetric with specified name, description, units, and value type.
   *
   * Initializes the metric with the provided parameters and sets up initial summary data.
   * TODO (https://jira.arm.com/browse/ASTL-97) : Add support for masks, formula to allow more complex processing of
   * metrics.
   *
   * @param name The name of the metric.
   * @param description A brief description of the metric.
   * @param units The units of measurement for this metric.
   * @param value_type The type of values this metric will process (e.g., UINT64).
   */
  explicit SampledValueMetric(const char *name, const char *description, astl_units_t units,
                              astl_value_type_t value_type);

  /**
   * @brief Process and record a new sample value.
   *
   * Updates the internal state by incorporating the given sample
   * into min, max, sum, and count statistics.
   *
   * @param sample A single sampled data point to be processed.
   * @return astl_status_code indicating success or failure.
   */
  astl_status_code ReceiveSample(const SampledData &sample) override;
  /**
   * @brief Summarize collected sample data.
   *
   * Finalizes the summary by calculating the average value from
   * accumulated samples. Logs the results using the summary logger.
   *
   * @return astl_status_code indicating success or failure.
   */
  astl_status_code Summarize() override;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  astl_status_code GetProperties(astl_metric_properties_t *properties) const override;

  /**
   * @brief Retrieve the statistical summary of the sampled values.
   *
   * Returns the current summary data containing minimum, maximum,
   * and average values computed from received samples.
   *
   * @return A SampledValueSummaryData struct with summary statistics.
   */
  MinMaxAvgSummaryData GetSummaryData() const;

 private:
  /** @brief private helper to update statistics for summary later */
  astl_status_code UpdateStatistics(const SampledData &sample);

  std::string       _name;
  std::string       _description;
  astl_units_t      _units;
  astl_value_type_t _value_type;

  MinMaxAvgSummaryData _summary_data;  // Summary data for min, max, avg
  AstlValue            _sum_sample_value{
      uint64_t{0}};  // Sum of sample values for average calculation. Uses uint64_t to reduce risk of overflow.
                     // uint64_t is the largest natively support integer type across Windows, macOS and Linux.
  uint64_t _sample_count;  // Count of samples received
  // Create a Logger instance explicitly to log samples
  astl::Logger _raw_sample_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                                  false /* No default formatting */, "sampled_value_raw.log"};
  // create a logger instance to explicitly log summary
  astl::Logger _summary_logger{astl::LogLevel::Info, false /* Console logging disabled */,
                               false /* No default formatting */, "sampled_value_summary.log"};

};  // End of SampledValueMetric class

}  // namespace astl

#endif  // SAMPLED_VALUE_METRIC_HPP_
