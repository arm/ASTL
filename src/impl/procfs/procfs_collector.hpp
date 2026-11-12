// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_COLLECTOR_HPP_
#define PROCFS_COLLECTOR_HPP_

#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "astl_cxx_compat.hpp"
#include "astl_file_interface.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "common/collection_lifecycle.hpp"
#include "operation/operation.hpp"
#include "procfs/procfs_read_operation.hpp"
#include "procfs/procfs_utils.hpp"

namespace astl {

/**
 * @brief Collector implementation for procfs-backed telemetry.
 */
class ProcfsCollector : public ICollector {
 public:
  using SampleClock = std::function<procfs::CounterRateState::Clock::time_point()>;
  explicit ProcfsCollector(FileInterface procfs_file_interface,
                           SampleClock   sample_clock = procfs::CounterRateState::Clock::now);
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
  auto GetNativeClockSnapshot() -> astl::compat::expected<ClockCorrelationMap, astl_status_code> override;
  auto ReadImmediate() -> astl_status_code override;

 private:
  using CollectionState = CollectionLifecycleState;

  using CpuSnapshotCache = std::unordered_map<std::filesystem::path, procfs::CpuSnapshotMap>;

  struct InterruptSnapshot {
    std::string           identity;
    std::vector<uint64_t> components;
    auto                  HasReset(const InterruptSnapshot& previous) const -> bool;
  };

  struct PreparedOperations {
    std::vector<const ProcfsReadOperation*>                                                operations;
    CpuSnapshotCache                                                                       cpu_snapshots;
    std::unordered_set<std::filesystem::path>                                              interrupt_resets;
    std::unordered_map<std::filesystem::path, procfs::InterruptCounterMap>                 interrupt_snapshots;
    std::unordered_map<std::filesystem::path, procfs::CounterRateState::Clock::time_point> interrupt_times;
    std::unordered_map<std::filesystem::path, std::string>                                 interrupt_identities;
  };

  auto PrepareInterruptSnapshot(const std::filesystem::path& path, PreparedOperations& prepared) -> astl_status_code;
  auto ReadSystemSample(const procfs::SystemField& field, const PreparedOperations& prepared,
                        std::string& source_identity, bool& missing_label)
      -> astl::compat::expected<AstlValue, astl_status_code>;
  auto ReadRateSample(OperationId operation_id, const procfs::SystemField& field, const AstlValue& value,
                      const PreparedOperations& prepared, std::string_view source_identity)
      -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code>;
  auto PrepareOperations(const OperationSequence& operations)
      -> astl::compat::expected<PreparedOperations, astl_status_code>;
  static auto FindCpuSnapshot(const procfs::CpuUtilizationField& cpu_field, const CpuSnapshotCache& cpu_snapshot_cache)
      -> astl::compat::expected<const procfs::CpuSnapshot*, astl_status_code>;
  auto ReadCpuUtilizationSample(const ProcfsReadOperation& operation, const procfs::CpuUtilizationField& cpu_field,
                                const procfs::CpuSnapshot& cpu_snapshot) -> astl::compat::optional<AstlValue>;
  auto ReadOperationSample(const ProcfsReadOperation& operation, const PreparedOperations& prepared)
      -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code>;
  auto ReadFieldSample(const ProcfsReadOperation& operation, const PreparedOperations& prepared)
      -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code>;
  auto ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code;
  auto ExecuteStartModeOperations() -> astl_status_code;
  auto ExecuteStopModeOperations() -> astl_status_code;
  auto StartIntervalSampling() -> astl_status_code;
  auto StopIntervalSampling() -> void;

  CollectorCapability                                          _collector_capability{CollectorType::PROCFS};
  FileInterface                                                _procfs_file_interface;
  SampleClock                                                  _sample_clock;
  IRawSampleSink*                                              _sample_sink{nullptr};
  CollectionState                                              _collection_state{CollectionState::UNCONFIGURED};
  astl::compat::optional<CollectionConfiguration>              _configuration;
  mutable std::mutex                                           _collection_mutex;
  std::unique_ptr<PeriodicSampler>                             _periodic_sampler;
  std::unordered_map<std::filesystem::path, InterruptSnapshot> _previous_interrupt_snapshots;
  std::unordered_map<OperationId, procfs::CounterRateState>    _counter_rates;
  std::unordered_map<OperationId, procfs::CpuSnapshot>         _previous_cpu_snapshots;
};

}  // namespace astl

#endif  // PROCFS_COLLECTOR_HPP_
