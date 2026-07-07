// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_COLLECTOR_HPP_
#define PROCFS_COLLECTOR_HPP_

#include <mutex>
#include <optional>
#include <unordered_map>

#include "astl_file_interface.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "common/procfs_utils.hpp"
#include "operation/operation.hpp"
#include "operation/procfs_read_operation.hpp"

namespace astl {

/**
 * @brief Collector implementation for procfs-backed telemetry.
 */
class ProcfsCollector : public ICollector {
 public:
  explicit ProcfsCollector(FileInterface procfs_file_interface);
  ~ProcfsCollector() override = default;

  ProcfsCollector(const ProcfsCollector&)            = delete;
  ProcfsCollector& operator=(const ProcfsCollector&) = delete;
  ProcfsCollector(ProcfsCollector&&)                 = delete;
  ProcfsCollector& operator=(ProcfsCollector&&)      = delete;

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
  enum class CollectionState { UNCONFIGURED, CONFIGURED, STARTED, PAUSED, STOPPED };

  auto ReadOperationSample(const ProcfsReadOperation& operation)
      -> std::expected<std::optional<AstlValue>, astl_status_code>;
  auto ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code;
  auto ExecuteStartModeOperations() -> astl_status_code;
  auto ExecuteStopModeOperations() -> astl_status_code;
  auto StartIntervalSampling() -> astl_status_code;
  auto StopIntervalSampling() -> void;

  CollectorCapability                                  _collector_capability{CollectorType::PROCFS};
  FileInterface                                        _procfs_file_interface;
  IRawSampleSink*                                      _sample_sink{nullptr};
  CollectionState                                      _collection_state{CollectionState::UNCONFIGURED};
  std::optional<CollectionConfiguration>               _configuration;
  mutable std::mutex                                   _collection_mutex;
  std::unique_ptr<PeriodicSampler>                     _periodic_sampler;
  std::unordered_map<OperationId, procfs::CpuSnapshot> _previous_cpu_snapshots;
};

}  // namespace astl

#endif  // PROCFS_COLLECTOR_HPP_
