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

#ifndef COLLECTOR_MANAGER_HPP_
#define COLLECTOR_MANAGER_HPP_

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "astl/astl.h"
#include "collector/collection_configuration.hpp"
#include "common/i_sample_sink.hpp"
#include "common/operation.hpp"
#include "config/astl_configuration.hpp"
#include "counter.hpp"
#include "i_collector.hpp"
#include "i_collector_manager.hpp"
#include "target.hpp"

namespace astl {

class CollectorManager : public ICollectorManager, public ISampleSink {
 public:
  CollectorManager() = delete;

  /**
   * @brief Construct the Collector manager, providing a list of targets discovered by the topology manager.
   *        The collector manager is responsible for assigning a specific collector to each target
   *
   * @param collectors is a map from a given target to a set of collectors than can retrieve samples from that target.
   *        It's assumed that each target supports only on CollectorType protocol, but there are multiple collector
   *        options based on collection optimization hints and heuristics.
   *
   * @note Collection must be stopped and CollectorManager destroyed before the given ITarget keys are destroyed
   */
  explicit CollectorManager(const std::vector<std::unique_ptr<ITarget>>&, const AstlConfiguration& configuration);

  // CollectorManager owns its ICollector instances, so it can be moved, but not copied
  CollectorManager(CollectorManager const&)            = delete;
  CollectorManager& operator=(CollectorManager const&) = delete;
  CollectorManager(CollectorManager&&)                 = default;
  CollectorManager& operator=(CollectorManager&&)      = default;

  ~CollectorManager() override = default;

  std::unordered_map<ITarget*, std::vector<CollectorCapability>> ReportCollectionCapabilities() const override;

  // ICollectorManager implementation
  astl_status_code RegisterSampleSink(ISampleSink* sink) override;
  astl_status_code UnregisterSampleSink(ISampleSink* sink) override;

  astl_status_code ConfigureCollectionOnTarget(ITarget* target, astl_collection_parameters_t const& collection_params,
                                               CollectionOperations&& operations) override;

  astl_status_code StartOnTarget(ITarget* target) override;

  astl_status_code PauseOnTarget(ITarget* target) override;

  astl_status_code ResumeOnTarget(ITarget* target) override;

  astl_status_code ReadImmediateOnTarget(ITarget* target) override;

  astl_status_code StopOnTarget(ITarget* target) override;

  // ISampleSink implementation
  astl_status_code SinkSamples(ITarget* target, std::span<SampledData> samples) override;

  // @todo ASTL-148 DEPRICATED!  DO NOT USE!
  astl_status_code ForceTargetToCollectorMap(
      std::unordered_map<ITarget*, std::vector<std::unique_ptr<ICollector>>>&& targets_to_collectors_map);

 private:
  std::unordered_set<ISampleSink*> _registered_sample_sinks;

  /// @todo ASTL-145 It's a bit of an anti-pattern to take a raw pointer to a unique_ptr for ITarget*
  std::unordered_map<ITarget*, std::vector<std::unique_ptr<ICollector>>> _collectors;

  /// given a target and a set of desired capabilities, choose a suitable collector
  /// @todo ASTL-145 It's a bit of an anti-pattern to take a raw pointer to a unique_ptr for ITarget*
  std::expected<ICollector*, astl_status_code> SelectCollector(ITarget*                   target,
                                                               CollectorCapability const& requirements);
};

}  // namespace astl

#endif  // COLLECTOR_MANAGER_HPP_
