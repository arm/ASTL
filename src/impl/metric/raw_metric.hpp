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

#ifndef RAW_METRIC_HPP_
#define RAW_METRIC_HPP_

#include "astl/astl.h"
#include "i_metric.hpp"

namespace astl {

/**
 * @brief Base class for raw metric types that process sampled data.
 *
 * RawMetric provides default behavior for capability checking and operation setup.
 * Specific metric types like sampled, delta, or residency should inherit from this class
 * and implement sample processing and summarization.
 */
class RawMetric : public IMetric {
 public:
  ~RawMetric() override = default;

  RawMetric()                             = default;
  RawMetric(const RawMetric &)            = default;
  RawMetric &operator=(const RawMetric &) = default;
  RawMetric(RawMetric &&)                 = default;
  RawMetric &operator=(RawMetric &&)      = default;

  /**
   * @brief Check if the Capabilities are met for this metric.
   * This method can be overridden by derived classes to implement specific capability checks.
   *
   * @param capabilities The capabilities to check against.
   * @return true if the capabilities are met, false otherwise.
   */
  bool CheckCapabilities(const Capabilities &capabilities) const override {
    return true;  // Default implementation, can be overridden by derived classes
  }

  /**
   * @brief Get the Operations required to the metric.
   * The API determine the collector protocol from the Metric Config and create the Operations.
   *
   * @return OperationSequence
   */
  std::expected<OperationSequence, astl_status_code> GetOperations() const override {
    // Default implementation returns an empty sequence
    return OperationSequence{};
  }

  astl_status_code ReceiveSample(const SampledData &sample) override = 0;

  astl_status_code Summarize() override = 0;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  astl_status_code GetProperties(astl_metric_properties_t *properties) const override = 0;

};  // End of RawMetric class

}  // namespace astl

#endif  // RAW_METRIC_HPP_
