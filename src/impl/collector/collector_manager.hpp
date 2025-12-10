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

#include "astl/astl.h"
#include "common/i_raw_sample_sink.hpp"
#include "i_collector.hpp"
#include "i_collector_manager.hpp"
#include "operation/operation.hpp"
#include "target.hpp"

namespace astl {

class CollectorManager : public ICollectorManager, public IRawSampleSink {
 public:
  CollectorManager() = delete;

  /*
   * @brief Construct the Collector manager, providing a set of collection strategies for each target.
   *
   * @param collectors is a map from a given target to a set of collectors than can retrieve samples from that target.
   *        It's assumed that each target supports only on CollectorType protocol, but there are multiple collector
   *        options based on collection optimization hints and heuristics.
   *
   * @note Collection must be stopped and CollectorManager destroyed before the given ITarget keys are destroyed
   */
  explicit CollectorManager(std::unordered_map<const ITarget*, std::vector<std::unique_ptr<ICollector>>>&& collectors);

  // CollectorManager owns its ICollector instances, so it can be moved, but not copied
  CollectorManager(CollectorManager const&)            = delete;
  CollectorManager& operator=(CollectorManager const&) = delete;
  CollectorManager(CollectorManager&&)                 = default;
  CollectorManager& operator=(CollectorManager&&)      = default;

  ~CollectorManager() override = default;

  auto ReportCollectionCapabilities() const
      -> std::unordered_map<const ITarget*, std::vector<CollectorCapability>> override;

  // ICollectorManager implementation
  auto RegisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code override;
  auto UnregisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code override;

  auto ConfigureCollectionOnTarget(const ITarget* target, astl_collection_parameters_t const& collection_params,
                                   CollectionOperations&& operations) -> astl_status_code override;

  auto StartOnTarget(const ITarget* target) -> astl_status_code override;

  auto PauseOnTarget(const ITarget* target) -> astl_status_code override;

  auto ResumeOnTarget(const ITarget* target) -> astl_status_code override;

  auto ReadImmediateOnTarget(const ITarget* target) -> astl_status_code override;

  auto StopOnTarget(const ITarget* target) -> astl_status_code override;

  // IRawSampleSink implementation
  auto SinkRawSamples(const ITarget* target, std::span<RawSampledData> samples) -> astl_status_code override;

 private:
  std::unordered_set<IRawSampleSink*> _registered_raw_sample_sinks;

  /// @todo ASTL-145 It's a bit of an anti-pattern to take a raw pointer to a unique_ptr for ITarget*
  std::unordered_map<const ITarget*, std::vector<std::unique_ptr<ICollector>>> _collectors;

  /// given a target and a set of desired capabilities, choose a suitable collector
  /// @todo ASTL-145 It's a bit of an anti-pattern to take a raw pointer to a unique_ptr for ITarget*
  auto SelectCollector(const ITarget* target, CollectorCapability const& requirements)
      -> std::expected<ICollector*, astl_status_code>;
};

}  // namespace astl

#endif  // COLLECTOR_MANAGER_HPP_
