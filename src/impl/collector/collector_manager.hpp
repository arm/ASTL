// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTOR_MANAGER_HPP_
#define COLLECTOR_MANAGER_HPP_

#include <mutex>
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
  CollectorManager(CollectorManager&&)                 = delete;
  CollectorManager& operator=(CollectorManager&&)      = delete;

  ~CollectorManager() override = default;

  [[nodiscard]] auto ReportCollectionCapabilities() const
      -> std::unordered_map<const ITarget*, std::vector<CollectorCapability>> override;

  // ICollectorManager implementation
  /**
   * @brief Register a sink for raw sample fan-out.
   *
   * Must not be called from inside a SinkRawSamples callback.
   */
  [[nodiscard]] auto RegisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code override;

  /**
   * @brief Unregister a previously registered raw sample sink.
   *
   * Must not be called from inside a SinkRawSamples callback.
   */
  [[nodiscard]] auto UnregisterRawSampleSink(IRawSampleSink* sink) -> astl_status_code override;

  [[nodiscard]] auto ConfigureCollectionOnTarget(const ITarget*                  target,
                                                 astl_collection_params_t const& collection_params,
                                                 CollectionOperations&& operations) -> astl_status_code override;

  [[nodiscard]] auto StartOnTarget(const ITarget* target) -> astl_status_code override;

  [[nodiscard]] auto PauseOnTarget(const ITarget* target) -> astl_status_code override;

  [[nodiscard]] auto ResumeOnTarget(const ITarget* target) -> astl_status_code override;

  [[nodiscard]] auto ReadImmediateOnTarget(const ITarget* target) -> astl_status_code override;
  [[nodiscard]] auto StopOnTarget(const ITarget* target) -> astl_status_code override;

  [[nodiscard]] auto IsAnyTargetBeingCollected() const -> bool override;

  // IRawSampleSink implementation
  [[nodiscard]] auto SinkRawSamples(const ITarget* target, std::span<RawSampledData> samples)
      -> astl_status_code override;

 private:
  std::unordered_set<IRawSampleSink*> _registered_raw_sample_sinks;

  std::unordered_map<const ITarget*, std::vector<std::unique_ptr<ICollector>>> _collectors;

  std::unordered_set<const ITarget*> _targets_with_active_collection;  // track which targets have active collection

  /**
   * @brief Protects collector maps, active-target tracking, and sink registration/fan-out.
   */
  mutable std::mutex _mutex;

  /**
   * @brief Given a target and required capabilities, choose a suitable collector.
   *
   * Requires: caller holds `_mutex`.
   *
   * @todo ASTL-145 It's a bit of an anti-pattern to take a raw pointer to a unique_ptr for ITarget*
   */
  auto SelectCollectorLocked(const ITarget* target, CollectorCapability const& requirements)
      -> std::expected<ICollector*, astl_status_code>;
};

}  // namespace astl

#endif  // COLLECTOR_MANAGER_HPP_
