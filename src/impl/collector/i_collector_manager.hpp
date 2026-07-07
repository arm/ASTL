// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_COLLECTOR_MANAGER_HPP_
#define I_COLLECTOR_MANAGER_HPP_

#include <expected>
#include <unordered_map>

#include "astl/astl.h"
#include "collection_operations.hpp"
#include "common/capabilities.hpp"
#include "common/clock_correlation.hpp"
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
  [[nodiscard]] virtual auto ConfigureCollectionOnTarget(const ITarget*                  target,
                                                         astl_collection_params_t const& collection_params,
                                                         CollectionOperations&& configuration) -> astl_status_code = 0;

  /**
   * @brief Discard configured collection operations from every collector.
   *
   * This is only valid when no target is actively collecting. It is used at global configure
   * reset boundaries before OperationIds are reused.
   */
  [[nodiscard]] virtual auto ClearConfiguredCollections() -> astl_status_code = 0;

  /* Start the configured collection for the given target */
  [[nodiscard]] virtual auto StartOnTarget(const ITarget* target) -> astl_status_code = 0;

  /**
   * @brief Take a paired per-operation clock snapshot for the configured collector on @p target.
   *
   * Returns a ClockCorrelationMap keyed by OperationId.  Each entry records a simultaneous
   * CLOCK_MONOTONIC_RAW and collector-native-clock snapshot so that MetricManager can later
   * translate raw timestamps to the common CLOCK_MONOTONIC_RAW reference.
   *
   * Must be called after ConfigureCollectionOnTarget and before StartOnTarget.
   * Returns std::unexpected on I/O or parse failure.
   */
  [[nodiscard]] virtual auto GetNativeClockSnapshot(const ITarget* target)
      -> std::expected<ClockCorrelationMap, astl_status_code> = 0;

  [[nodiscard]] virtual auto PauseOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto ResumeOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto ReadImmediateOnTarget(const ITarget* target) -> astl_status_code = 0;

  [[nodiscard]] virtual auto StopOnTarget(const ITarget* target) -> astl_status_code = 0;

  // Check if any collector is currently started or paused on any target
  [[nodiscard]] virtual auto IsAnyTargetBeingCollected() const -> bool = 0;
};

}  // namespace astl

#endif  // I_COLLECTOR_MANAGER_HPP_
