// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WINDOWS_PDH_SYSTEM_COLLECTOR_HPP_
#define WINDOWS_PDH_SYSTEM_COLLECTOR_HPP_

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "common/collection_lifecycle.hpp"
#include "operation/operation.hpp"
#include "windows_pdh/windows_pdh_operation_builder.hpp"
#include "windows_pdh/windows_pdh_system_api.hpp"

namespace astl {

/**
 * @brief Collector implementation for Windows PDH system telemetry.
 */
class WindowsPdhSystemCollector : public ICollector {
 public:
  explicit WindowsPdhSystemCollector(std::unique_ptr<IWindowsPdhSystemApi> api);
  ~WindowsPdhSystemCollector() override;

  WindowsPdhSystemCollector(const WindowsPdhSystemCollector&)            = delete;
  WindowsPdhSystemCollector& operator=(const WindowsPdhSystemCollector&) = delete;
  WindowsPdhSystemCollector(WindowsPdhSystemCollector&&)                 = delete;
  WindowsPdhSystemCollector& operator=(WindowsPdhSystemCollector&&)      = delete;

  auto GetCapabilities() const -> CollectorCapability override;
  auto SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void override;
  auto ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code override;
  auto ClearCollectionState() -> astl_status_code override;
  auto StartCollection() -> astl_status_code override;
  auto PauseCollection() -> astl_status_code override;
  auto ResumeCollection() -> astl_status_code override;
  auto StopCollection() -> astl_status_code override;
  auto GetNativeClockSnapshot() -> std::expected<ClockCorrelationMap, astl_status_code> override;
  auto ReadImmediate() -> astl_status_code override;

 private:
  using CollectionState = CollectionLifecycleState;

  auto ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code;
  auto ExecuteStartModeOperations() -> astl_status_code;
  auto ExecuteStopModeOperations() -> astl_status_code;
  auto StartIntervalSampling() -> astl_status_code;
  auto StopIntervalSampling() -> void;

  CollectorCapability                    _collector_capability{CollectorType::WINDOWS_PDH};
  std::unique_ptr<IWindowsPdhSystemApi>  _api;
  IRawSampleSink*                        _sample_sink{nullptr};
  CollectionState                        _collection_state{CollectionState::UNCONFIGURED};
  std::optional<CollectionConfiguration> _configuration;
  mutable std::mutex                     _collection_mutex;
  std::unique_ptr<PeriodicSampler>       _periodic_sampler;
};

}  // namespace astl

#endif  // WINDOWS_PDH_SYSTEM_COLLECTOR_HPP_
