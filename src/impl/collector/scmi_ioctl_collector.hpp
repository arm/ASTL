// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_IOCTL_COLLECTOR_HPP_
#define SCMI_IOCTL_COLLECTOR_HPP_

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "collector/scmi_data_event.hpp"
#include "collector/scmi_ioctl_interface.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "operation/operation.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl {

/**
 * @brief SCMI collector implementation backed by the telemetry ioctl character-device interface.
 *
 * The collector mirrors the legacy sysfs collector lifecycle while using
 * SCMI_TLM_* ioctls to enable telemetry, enable data events, read metadata, and
 * collect samples from one `/dev/scmi/tlm_N` target.
 */
class ScmiIoctlCollector : public ICollector {
 public:
  ScmiIoctlCollector() = delete;

  /**
   * @brief Creates a collector for one SCMI telemetry ioctl device.
   *
   * @param device_path Path to a telemetry character device, such as `/dev/scmi/tlm_0`.
   */
  explicit ScmiIoctlCollector(std::filesystem::path device_path);

  /**
   * @brief Creates a collector using a caller-provided SCMI ioctl interface implementation.
   *
   * This constructor is used by tests to exercise collector behavior without a real SCMI telemetry device.
   */
  explicit ScmiIoctlCollector(std::unique_ptr<IScmiIoctlInterface> scmi_ioctl_interface);

  /** @brief Stops active collection and restores data event state before destruction. */
  ~ScmiIoctlCollector() override;

  ScmiIoctlCollector(const ScmiIoctlCollector&)            = delete;
  ScmiIoctlCollector& operator=(const ScmiIoctlCollector&) = delete;
  ScmiIoctlCollector(ScmiIoctlCollector&&)                 = delete;
  ScmiIoctlCollector& operator=(ScmiIoctlCollector&&)      = delete;

  /**
   * @brief Reports the collector capability advertised for SCMI targets.
   *
   * @return SCMI collector capability descriptor.
   */
  CollectorCapability GetCapabilities() const override;

  /**
   * @brief Registers the sink that receives raw samples produced by this collector.
   *
   * @param raw_sample_sink Sink to receive raw samples, or null to disable forwarding.
   */
  void SetRawSampleSink(IRawSampleSink* raw_sample_sink) override;

  /**
   * @brief Configures collection operations and enables their required SCMI data events.
   *
   * @param configuration Collection configuration containing the target, parameters, and operations.
   * @return ASTL_STATUS_SUCCESS on success, or the first setup failure.
   */
  astl_status_code ConfigureCollection(CollectionConfiguration&& configuration) override;

  /**
   * @brief Clears configured collection state when collection is not active.
   *
   * @return ASTL_STATUS_SUCCESS on success, or ASTL_STATUS_COLLECTION_ALREADY_RUNNING when active.
   */
  astl_status_code ClearCollectionState() override;

  /**
   * @brief Starts the configured collection according to the selected collection mode.
   *
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status describing why start failed.
   */
  astl_status_code StartCollection() override;

  /**
   * @brief Pauses periodic sampling and emits a pause marker sample.
   *
   * @return ASTL_STATUS_SUCCESS on success, or a sink failure status.
   */
  astl_status_code PauseCollection() override;

  /**
   * @brief Emits a resume marker sample and resumes periodic sampling.
   *
   * @return ASTL_STATUS_SUCCESS on success, or a sink failure status.
   */
  astl_status_code ResumeCollection() override;

  /**
   * @brief Stops collection, runs stop operations, and restores modified data event state.
   *
   * @return ASTL_STATUS_SUCCESS on success, or the first stop or restore failure.
   */
  astl_status_code StopCollection() override;

  /**
   * @brief Executes the configured sample operations once for immediate reads.
   *
   * @return ASTL_STATUS_SUCCESS on success, or an operation failure status.
   */
  astl_status_code ReadImmediate() override;

  /**
   * @brief Builds clock-correlation snapshots for configured SCMI read operations.
   *
   * @return Clock correlation map, or an ASTL status when an ioctl read fails.
   */
  std::expected<ClockCorrelationMap, astl_status_code> GetNativeClockSnapshot() override;

 private:
  /** @brief Internal collection lifecycle states. */
  enum class CollectionState { UNCONFIGURED, CONFIGURED, STARTED, PAUSED, STOPPED };

  /** @brief Marker sample type emitted when collection is paused or resumed. */
  enum class PauseResumeMarker { PAUSE, RESUME };

  /**
   * @brief Enables the telemetry target through SCMI_TLM_GET_CFG and SCMI_TLM_SET_CFG.
   *
   * @return ASTL_STATUS_SUCCESS on success, or an ioctl failure status.
   */
  auto EnableTelemetry() -> astl_status_code;

  /**
   * @brief Probes the ioctl device and logs the capabilities used by collection.
   *
   * @return ASTL_STATUS_SUCCESS on success, or the first probe failure.
   */
  auto ProbeCapabilities() -> astl_status_code;

  /**
   * @brief Enables telemetry and prepares the requested collection configuration.
   *
   * @param configuration Collection configuration to activate.
   * @return ASTL_STATUS_SUCCESS on success, or the first setup failure.
   */
  auto PrepareConfiguration(CollectionConfiguration&& configuration) -> astl_status_code;

  /**
   * @brief Enables each requested data event and records its original state.
   *
   * @param data_events_to_enable SCMI data event identifiers required by the configured operations.
   * @return Original data-event states to restore later, or an ASTL status on failure.
   */
  auto EnableDataEvents(std::unordered_set<ScmiDataEventId> const& data_events_to_enable)
      -> std::expected<std::vector<ScmiDataEvent>, astl_status_code>;

  /**
   * @brief Restores data event enable and timestamp-enable state saved during configuration.
   *
   * @param data_events Original data-event states captured by EnableDataEvents().
   * @return ASTL_STATUS_SUCCESS on success, or the first restore failure.
   */
  auto RestoreDataEventEnabledState(std::vector<ScmiDataEvent> const& data_events) -> astl_status_code;

  /**
   * @brief Executes a sequence of configured SCMI read operations.
   *
   * @param operations Operations to execute in order.
   * @return ASTL_STATUS_SUCCESS on success, or the first operation failure.
   */
  auto ExecuteCollectionOperations(OperationSequence const& operations) -> astl_status_code;

  /**
   * @brief Samples a sequence, using a platform-triggered single read when supported.
   */
  auto SampleCollectionOperations(OperationSequence const& operations) -> astl_status_code;

  /**
   * @brief Matches samples returned by SCMI_TLM_SINGLE_READ to configured read operations.
   */
  auto ExecuteSingleReadOperations(OperationSequence const& operations) -> astl_status_code;

  /**
   * @brief Reads the samples returned by one SCMI_TLM_SINGLE_READ request.
   *
   * @return Returned samples, or an ioctl failure status.
   */
  auto ReadSingleSamples() -> std::expected<std::vector<scmi_tlm_de_sample>, astl_status_code>;

  /**
   * @brief Matches single-read samples to operations and emits them.
   *
   * @param operations Configured operations to satisfy.
   * @param samples Samples returned by SCMI_TLM_SINGLE_READ.
   * @return ASTL_STATUS_SUCCESS on success, or the first matching or sink failure.
   */
  auto EmitSingleReadSamples(OperationSequence const& operations, std::vector<scmi_tlm_de_sample> const& samples)
      -> astl_status_code;

  /**
   * @brief Reads one SCMI data event and forwards the resulting sample to the raw sample sink.
   *
   * @param operation SCMI read operation describing the data event and output sample id.
   * @return ASTL_STATUS_SUCCESS on success, or an ioctl or sink failure status.
   */
  auto ExecuteScmiReadOperation(ScmiReadOperation const& operation) -> astl_status_code;

  /**
   * @brief Converts one ioctl sample into an ASTL raw sample and forwards it to the sink.
   */
  auto EmitScmiSample(ScmiReadOperation const& operation, const scmi_tlm_de_sample& sample) -> astl_status_code;

  /**
   * @brief Starts periodic sampling for sampling-mode collection.
   *
   * @return ASTL_STATUS_SUCCESS on success, or a sampler startup failure.
   */
  auto StartIntervalSampling() -> astl_status_code;

  /**
   * @brief Validates that the collector can start periodic sampling.
   *
   * @return ASTL_STATUS_SUCCESS when sampling can start, otherwise ASTL_STATUS_BAD_CONFIGURATION.
   */
  auto CheckStartIntervalSampling() const -> astl_status_code;

  /**
   * @brief Emits a pause or resume marker sample to the raw sample sink.
   *
   * @param marker_type Marker kind to emit.
   * @param timestamp Monotonic raw timestamp associated with the marker.
   * @return ASTL_STATUS_SUCCESS on success, or a sink failure status.
   */
  auto EmitPauseResumeSample(PauseResumeMarker marker_type, ProcessedSampleTimestamp timestamp) -> astl_status_code;

  /** @brief Stops periodic sampling by releasing the sampler instance. */
  void StopIntervalSampling();

  /**
   * @brief Restores configured data-event state and returns the collector to UNCONFIGURED.
   *
   * This helper is noexcept so it can safely be called from cleanup paths.
   *
   * @param rollback_context Human-readable context used in warning logs.
   */
  void RollbackConfigurationState(const char* rollback_context) noexcept;

  /** @brief Static collector capability advertised for SCMI targets. */
  CollectorCapability _collector_capability{CollectorType::SCMI};

  /** @brief Sink receiving raw samples produced by this collector. */
  IRawSampleSink* _raw_sample_sink = nullptr;

  /** @brief Current collection lifecycle state. */
  CollectionState _collection_state{CollectionState::UNCONFIGURED};

  /** @brief Active collection configuration, present after successful ConfigureCollection(). */
  std::optional<CollectionConfiguration> _configuration;

  /** @brief Ioctl interface for the target telemetry device. */
  std::unique_ptr<IScmiIoctlInterface> _scmi_ioctl_interface;

  /** @brief Data events enabled by this collector and their original state. */
  std::vector<ScmiDataEvent> _data_events;

  /** @brief Mutex protecting collection state and configuration. */
  mutable std::mutex _collection_mutex;

  /** @brief Periodic sampler used only for ASTL_COLLECTION_MODE_SAMPLING. */
  std::unique_ptr<PeriodicSampler> _periodic_sampler;

  /** @brief Last emitted timestamp for each data event, used to discard duplicates. */
  std::unordered_map<ScmiDataEventId, HwClockTicks> _previous_timestamps;

  /** @brief true when samples use CLOCK_MONOTONIC_RAW instead of SCMI hardware timestamps. */
  bool _use_software_clock_timestamps{false};
};

}  // namespace astl

#endif  // SCMI_IOCTL_COLLECTOR_HPP_
