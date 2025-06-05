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

#include <map>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "astl/astl.h"
#include "collector/collection_configuration.hpp"
#include "common/i_sample_sink.hpp"
#include "common/operation.hpp"
#include "counter.hpp"
#include "i_collector.hpp"
#include "i_collector_manager.hpp"
#include "target.hpp"

namespace astl {

class CollectorManager : public ICollectorManager, public ISampleSink {
 public:
  CollectorManager() = delete;
  /*
   * operationsProvider knows how to translate groups of metrics into sets of operations for a given target.
   */
  explicit CollectorManager(std::unordered_map<ITarget*, std::vector<std::unique_ptr<ICollector>>>&& collectors);

  // CollectorManager owns its ICollector instances, so it can be moved, but not copied
  CollectorManager(CollectorManager const&)            = delete;
  CollectorManager& operator=(CollectorManager const&) = delete;
  CollectorManager(CollectorManager&&)                 = default;
  CollectorManager& operator=(CollectorManager&&)      = default;

  ~CollectorManager() override = default;

  std::unordered_map<ITarget*, std::vector<CollectorCapabilities>> ReportCollectionCapabilities() const override;

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

 private:
  std::unordered_set<ISampleSink*>                                       _registered_sample_sinks;
  std::unordered_map<ITarget*, std::vector<std::unique_ptr<ICollector>>> _collectors;

  // given a target and a set of desired capabilities, choose a suiteable collector
  std::expected<ICollector*, astl_status_code> SelectCollector(ITarget*                     target,
                                                               CollectorCapabilities const& requirements);
};

}  // namespace astl

#endif  // COLLECTOR_MANAGER_HPP_