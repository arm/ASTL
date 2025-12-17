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

#ifndef I_COLLECTOR_MANAGER_HPP_
#define I_COLLECTOR_MANAGER_HPP_

#include <unordered_map>

#include "astl/astl.h"
#include "collection_operations.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "target.hpp"

namespace astl {

/*
 * An owner and manager of a group of ICollector instances.
 * Can report target collection capabilities, and manages the configuration of collection.
 * It tracks the state of what is being actively collected on which targets.
 */
struct ICollectorManager {
  virtual ~ICollectorManager() = default;

  ICollectorManager()                                    = default;
  ICollectorManager(const ICollectorManager&)            = default;
  ICollectorManager& operator=(const ICollectorManager&) = default;
  ICollectorManager(ICollectorManager&&)                 = default;
  ICollectorManager& operator=(ICollectorManager&&)      = default;

  [[nodiscard]] virtual auto ReportCollectionCapabilities() const
      -> std::unordered_map<const ITarget*, std::vector<CollectorCapability>> = 0;

  /* The CollectorManager can support a number of destinations for raw sampled data to go to.
   * This might include Orchestrator, a logger, a think translator for a API callback, etc.
   * When data is sampled, or generated asynchronously, it'll be sent to each IRawSampleSink in turn
   */
  [[nodiscard]] virtual auto RegisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code   = 0;
  [[nodiscard]] virtual auto UnregisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code = 0;

  /* CollectorManager should choose a suitable collector for the given operations and target,
   * and enable it according to the collection parameters.
   */
  [[nodiscard]] virtual auto ConfigureCollectionOnTarget(const ITarget*                      target,
                                                         astl_collection_parameters_t const& collection_params,
                                                         CollectionOperations&& configuration) -> astl_status_code = 0;

  /* Start the configured collection for the given target */
  [[nodiscard]] virtual auto StartOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto PauseOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto ResumeOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto ReadImmediateOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto StopOnTarget(const ITarget* target) -> astl_status_code = 0;

  // Check if any collector is currently started or paused on any target
  [[nodiscard]] virtual auto IsAnyTargetBeingCollected() const -> bool = 0;
};

}  // namespace astl

#endif  // I_COLLECTOR_MANAGER_HPP_
