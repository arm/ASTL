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

#include <span>
#include <unordered_map>

#include "astl/astl.h"
#include "collection_operations.hpp"
#include "common/capabilities.hpp"
#include "common/i_sample_sink.hpp"
#include "counter.hpp"
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

  virtual std::unordered_map<ITarget*, std::vector<CollectorCapability>> ReportCollectionCapabilities() const = 0;

  /* The CollectorManager can support a number of destinations for sampled data to go to.
   * This might include Orchestrator, a logger, a think translator for a API callback, etc.
   * When data is sampled, or generated asynchronously, it'll be sent to each ISampleSink in turn
   */
  virtual astl_status_code RegisterSampleSink(ISampleSink* sink)   = 0;
  virtual astl_status_code UnregisterSampleSink(ISampleSink* sink) = 0;

  /* CollectorManager should choose a suitable collector for the given operations and target,
   * and enable it according to the collection parameters.
   */
  virtual astl_status_code ConfigureCollectionOnTarget(ITarget*                            target,
                                                       astl_collection_parameters_t const& collection_params,
                                                       CollectionOperations&&              configuration) = 0;

  /* Start the configured collection for the given target */
  virtual astl_status_code StartOnTarget(ITarget* target) = 0;

  virtual astl_status_code PauseOnTarget(ITarget* target) = 0;

  virtual astl_status_code ResumeOnTarget(ITarget* target) = 0;

  virtual astl_status_code ReadImmediateOnTarget(ITarget* target) = 0;

  virtual astl_status_code StopOnTarget(ITarget* target) = 0;
};

}  // namespace astl

#endif  // I_COLLECTOR_MANAGER_HPP_