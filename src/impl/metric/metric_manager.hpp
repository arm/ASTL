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

#ifndef METRIC_MANAGER_HPP_
#define METRIC_MANAGER_HPP_

#include <span>

#include "capabilities.hpp"
#include "i_metric_manager.hpp"

namespace astl {

class MetricManager : public IMetricManager {
 public:
  ~MetricManager() override = default;

  // MetricManager cannot be copied
  MetricManager()                                = delete;
  MetricManager(const MetricManager&)            = delete;
  MetricManager& operator=(const MetricManager&) = delete;
  MetricManager(MetricManager&&)                 = default;
  MetricManager& operator=(MetricManager&&)      = default;

  explicit MetricManager(const Capabilities& capabilities) : _capabilities(capabilities) {}

  void RegisterMetric(std::unique_ptr<MetricConfig> metric_config) override;

  std::span<IMetric*> GetAvailableMetrics() const override;

  std::expected<OperationSequence, astl_status_code> GetRequiredOperations(std::span<IMetric*> metrics) override;

  void ProcessData(std::span<SampledData> data) override;

  void SummarizeMetrics() override;

 private:
  std::vector<std::unique_ptr<IMetric>> _metrics;
  Capabilities                          _capabilities;
};

}  // namespace astl

#endif  // METRIC_MANAGER_HPP_