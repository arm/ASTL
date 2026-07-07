// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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

  astl_status_code ClearCollectionState() override;

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
   * @brief Collect a single sample of all the configured metrics.
   */
  astl_status_code ReadImmediate() override;

  /**
   * @brief Take a paired per-operation clock snapshot (CLOCK_MONOTONIC_RAW + steady_clock).
   *        All libsensors operations share the same steady_clock source, so a single pair of
   *        snapshots is applied to every OperationId in operationsOnSample.
   */
  std::expected<ClockCorrelationMap, astl_status_code> GetNativeClockSnapshot() override;

 private:
  // internal classes + enums

  enum class CollectionState { UNCONFIGURED, CONFIGURED, STARTED, PAUSED, STOPPED };
  enum class PauseResumeMarker { PAUSE, RESUME };

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
  auto             CheckStartIntervalSampling() const -> astl_status_code;

  /*
   * @brief Emit a pause or resume marker sample to the raw-sample sink.
   */
  astl_status_code EmitPauseResumeSample(PauseResumeMarker marker_type, ProcessedSampleTimestamp timestamp);

  /*
   * @brief Stop any background threads or async tasks that were started for interval sampling.
   */
  void StopIntervalSampling();
};

}  // namespace astl

#endif  // SENSORS_COLLECTOR_HPP_
