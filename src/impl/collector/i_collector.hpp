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

#ifndef I_COLLECTOR_HPP_
#define I_COLLECTOR_HPP_

#include "collection_configuration.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"

namespace astl {

/*
 * @brief The interface used by CollectorManager to configure/start/stop metric collection.
 *         ICollector instances typically operate on a single target and collection interface (e.g. SCMI).
 *
 */
struct ICollector {
  virtual ~ICollector() = default;

  ICollector()                             = default;
  ICollector(const ICollector&)            = default;
  ICollector& operator=(const ICollector&) = default;
  ICollector(ICollector&&)                 = default;
  ICollector& operator=(ICollector&&)      = default;

  /*
   * @brief Get the capabilities of this collector, including the collector type.
   */
  virtual auto GetCapabilities() const -> CollectorCapability = 0;

  /*
   * @brief Set the destination for where raw sampled data should be sent.
   *       This is typically the CollectorManager, but can be any IRawSampleSink.
   */
  virtual auto SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void = 0;

  /*
   * @brief Configure the collector to collect data, but don't start sampling it yet.
   *
   * @param configuration The configuration to apply to this collector, including the set of operations to run,
   *        the interval to sample at.
   */
  virtual auto ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code = 0;

  /*
   * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
   */
  virtual auto StartCollection() -> astl_status_code = 0;

  /*
   * @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact.
   */
  virtual auto PauseCollection() -> astl_status_code = 0;

  /*
   * @brief Resume the collection of data, starting any async tasks
   */
  virtual auto ResumeCollection() -> astl_status_code = 0;

  /*
   * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
   */
  virtual auto StopCollection() -> astl_status_code = 0;

  /*
   * @brief Collect a single sample of all the configured metics.
   */
  virtual auto ReadImmediate() -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_COLLECTOR_HPP_
