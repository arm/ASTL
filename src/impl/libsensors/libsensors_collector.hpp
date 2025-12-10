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

#ifndef SENSORS_COLLECTOR_HPP_
#define SENSORS_COLLECTOR_HPP_

#include <mutex>
#include <optional>

#include "astl_logger.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "libsensors/libsensors_api.hpp"
#include "operation/operation.hpp"

namespace astl {
/*
 * @brief A specialization of ICollector that interracts with the libsensors library for HWMON telemetry
 *
 * based on https://linux.die.net/man/3/libsensors
 * requires package 'libsensors-dev'
 */
class LibsensorsCollector : public ICollector {
 public:
  explicit LibsensorsCollector(std::shared_ptr<SensorsApi> sensors_api);
  ~LibsensorsCollector() override = default;

  LibsensorsCollector(const LibsensorsCollector&)            = delete;
  LibsensorsCollector& operator=(const LibsensorsCollector&) = delete;
  LibsensorsCollector(LibsensorsCollector&&)                 = delete;
  LibsensorsCollector& operator=(LibsensorsCollector&&)      = delete;

  /*
   * @brief Get the capabilities of this collector, including the collector type.
   */
  CollectorCapability GetCapabilities() const override;

  /*
   * @brief Set the destination for where sampled data should be sent.
   *       This is typically the CollectorManager, but can be any ISampleSink.
   */
  void SetRawSampleSink(IRawSampleSink* raw_sample_sink) override;

  /*
   * @brief Configure the collector to collect data, but don't start sampling it yet.
   *
   * @param configuration The configuration to apply to this collector, including the set of operations to run,
   *        the interval to sample at.
   */
  astl_status_code ConfigureCollection(CollectionConfiguration&& configuration) override;

  /*
   * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
   */
  astl_status_code StartCollection() override;

  /*
   * @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact.
   */
  astl_status_code PauseCollection() override;

  /*
   * @brief Resume the collection of data, starting any async tasks
   */
  astl_status_code ResumeCollection() override;

  /*
   * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
   */
  astl_status_code StopCollection() override;

  /*
   * @brief Collect a single sample of all the configured metics.
   */
  astl_status_code ReadImmediate() override;

 private:
  // internal classes + enums

  enum class CollectionState { UNCONFIGURED, STOPPED, STARTED, PAUSED };

  // data members

  CollectorCapability _collector_capability{CollectorType::LIBSENSORS};  //!< The capabilities of this collector
  IRawSampleSink*     _sample_sink = nullptr;  //!< The (optional) destination for where sampled data should be sent
  CollectionState     _collection_state = CollectionState::UNCONFIGURED;
  std::optional<CollectionConfiguration> _configuration;  //!< The current active configuration for this collector

  // prevent the collection configuration from being accessed by two threads at once
  mutable std::mutex               _collection_mutex;
  std::unique_ptr<PeriodicSampler> _periodic_sampler;
  std::shared_ptr<SensorsApi>      _sensors_api;  //!< The shared libsensors API instance

  // private methods

  /*
   * @brief Casts a sequence of abstract operations to ScmiReadOperation and executes them.
   */
  astl_status_code ExecuteCollectionOperations(OperationSequence const& operations);

  /*
   * @brief Initialize any threads or async tasks needed for interval sampling.
   */
  astl_status_code StartIntervalSampling();

  /*
   * @brief Stop any background threads or async tasks that were started for interval sampling.
   */
  void StopIntervalSampling();
};

}  // namespace astl

#endif  // SENSORS_COLLECTOR_HPP_
