// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_COLLECTOR_HPP_
#define I_COLLECTOR_HPP_

#include <expected>

#include "collection_configuration.hpp"
#include "common/capabilities.hpp"
#include "common/clock_correlation.hpp"
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
   * @brief Discard any configured collection operations and return to an unconfigured state.
   *
   * This must only succeed when collection is not actively started or paused. It is used at
   * global configure reset boundaries before OperationIds are reused.
   */
  virtual auto ClearCollectionState() -> astl_status_code = 0;

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
   * @brief Take a paired snapshot of CLOCK_MONOTONIC_RAW and this collector's native clock for every
   *        configured sample operation, returning a per-OperationId correlation map.
   *
   * Must be called after ConfigureCollection and before (or at) StartCollection so that all
   * operations and their clock sources are known.  The implementation reads from the hardware
   * counter (SCMI) or the host steady clock (libsensors) once per operation.
   */
  virtual auto GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> = 0;

  /*
   * @brief Collect a single sample of all the configured metrics, including while a collection is paused.
   */
  virtual auto ReadImmediate() -> astl_status_code = 0;
};

}  // namespace astl

#endif  // I_COLLECTOR_HPP_
