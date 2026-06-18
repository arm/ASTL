// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_SYSFS_COLLECTOR_HPP_
#define SCMI_SYSFS_COLLECTOR_HPP_

#include <charconv>
#include <expected>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "astl_file_interface.hpp"
#include "astl_logger.hpp"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/periodic_sampler.hpp"
#include "collector/scmi_data_event.hpp"
#include "common/capabilities.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "common/scmi/scmi_constants.hpp"
#include "filesystem_process_lock.hpp"
#include "operation/operation.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl {

/*
 * @brief A specialization of ICollector that interracts with the Scmi Sysfs system to fullfil SCMI operations
 *        in the case of multi-socket SCMI, there should be multiple instances of this collector,
 *        mapped in the Orchestrator by different Target instances
 */
template <typename FileInterfaceT>
class ScmiSysfsCollector : public ICollector {
 public:
  ~ScmiSysfsCollector() override;

  ScmiSysfsCollector() = delete;  // needs to be initialized with the base path for the telemetry directory
  explicit ScmiSysfsCollector(FileInterfaceT file_interface);

  ScmiSysfsCollector(const ScmiSysfsCollector&)            = delete;
  ScmiSysfsCollector& operator=(const ScmiSysfsCollector&) = delete;
  ScmiSysfsCollector(ScmiSysfsCollector&&)                 = delete;
  ScmiSysfsCollector& operator=(ScmiSysfsCollector&&)      = delete;

  /*
   * @brief Get the capabilities of this collector, including the collector type.
   */
  CollectorCapability GetCapabilities() const override;

  /*
   * @brief Set the destination for where sampled data should be sent.
   *       This is typically the CollectorManager, but can be any IRawSampleSink.
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
   * @brief Collect a single sample of all the configured metrics.
   */
  astl_status_code ReadImmediate() override;

  /**
   * @brief Take a paired per-operation clock snapshot (CLOCK_MONOTONIC_RAW + SCMI hardware counter)
   *        for every operation in operationsOnSample.  Each SCMI data event has its own hardware timer,
   *        so each OperationId receives an independent correlation entry.
   */
  std::expected<ClockCorrelationMap, astl_status_code> GetNativeClockSnapshot() override;

 private:
  // internal classes + enums

  enum class CollectionState { UNCONFIGURED, CONFIGURED, STARTED, PAUSED, STOPPED };
  enum class PauseResumeMarker { PAUSE, RESUME };

  // data members

  CollectorCapability _collector_capability{CollectorType::SCMI};  //!< The capabilities of this collector
  IRawSampleSink*     _raw_sample_sink = nullptr;  //!< The (optional) destination for where sampled data should be sent
  CollectionState     _collection_state = CollectionState::UNCONFIGURED;
  std::optional<CollectionConfiguration> _configuration;  //!< The current active configuration for this collector
  FileInterfaceT                         _scmi_file_interface;
  //!< Data events touched by the current collection (TODO - consider making _collection_state a variant to bundle the
  //!< CollectionState in with _data_events and _configuration)
  std::vector<ScmiDataEvent> _data_events;
  mutable std::mutex
      _collection_mutex;  // prevent the collection configuration from being accessed by two threads at once
  std::unique_ptr<PeriodicSampler> _periodic_sampler;
  std::unordered_map<ScmiDataEventId, HwClockTicks>
       _previous_timestamps;            //!< Track previous raw_tick per data event ID to detect duplicates
  bool _holds_process_lock_ref{false};  //!< True when this collector instance has retained one shared lock reference.
  //!< When true (ASTL_SCMI_USE_SOFTWARE_CLOCK_TIMESTAMPS is set), use CLOCK_MONOTONIC_RAW instead of the SCMI
  //!< hardware counter for all sample timestamps. tstamp_enable is not written to sysfs in this mode.
  bool _use_software_clock_timestamps{false};

  // private methods

  /*
   * @brief Enable a given data event, and return whether it was originally enabled before we enabled it.
   */
  auto EnableDataEvent(std::filesystem::path const& data_event_dir_path) -> std::expected<bool, astl_status_code>;

  /* @brief Enable a timestamp for this data event */
  auto EnableTimestamp(std::filesystem::path const& data_event_to_configure)
      -> std::expected<std::optional<bool>, astl_status_code>;

  /* @brief Read the timestamp rate for this data event, if available.
   * This is the rate at which the timestamp increments
   */
  auto ReadTimestampRate(std::filesystem::path const& data_event_to_configure)
      -> std::expected<std::optional<kilohertz>, astl_status_code>;

  /*
   * @brief Enable the given data event ids, returning the set of DataEvents and their initial enable state
   */
  std::expected<std::vector<ScmiDataEvent>, astl_status_code> EnableDataEvents(
      std::unordered_set<ScmiDataEventId> const& data_events_to_enable);

  /*
   * @brief Restore the original 'enable' value, disabling all data events that were originally disabled
   *        Also restores the tstamp_enable file
   */
  astl_status_code RestoreDataEventEnabledState(std::vector<ScmiDataEvent> const& data_events);

  /*
   * @brief Casts a sequence of abstract operations to ScmiReadOperation and executes them.
   */
  astl_status_code ExecuteCollectionOperations(OperationSequence const& operations);

  /*
   * @brief Execute a single Scmi read operation, creates a new RawSampledData object from the read value
   * and sends it to the sample sink.
   */
  astl_status_code ExecuteScmiReadOperation(ScmiReadOperation const& operation);

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

  /**
   * @brief Acquire (or validate) the process-level SCMI sysfs lock.
   * @return ASTL_STATUS_SUCCESS on success, otherwise a lock/open failure status.
   */
  astl_status_code AcquireProcessLock();

  /**
   * @brief Release the process-level SCMI sysfs lock, if currently held.
   */
  void ReleaseProcessLock() noexcept;

  /**
   * @brief Roll back collector configuration state, including enabled SCMI data events.
   *
   * @param rollback_context Context string used in warning logs when restore fails.
   */
  void RollbackConfigurationState(const char* rollback_context) noexcept;

  /**
   * @brief SCMI-aware write helper.
   *
   * Reads the current file content first. If it already matches @p value, no write is attempted.
   * If the file is not writable, logs a warning and continues successfully.
   * If a write is required, acquires the process-scoped filesystem lock before writing.
   */
  astl_status_code ScmiWrite(std::filesystem::path const& path, std::string_view value);

  struct SharedProcessLockState {
    std::mutex                                             mutex;
    std::unique_ptr<FilesystemProcessLock<FileInterfaceT>> lock;
    size_t                                                 refcount{0};
  };

  /**
   * @brief Accessor for process-scoped lock state shared by collector instances of this FileInterfaceT.
   */
  static auto GetSharedProcessLockState() -> SharedProcessLockState&;
};

////////////////////////////////////////////////////////////////////////////////
// Internal helpers, not part of this class's public API
////////////////////////////////////////////////////////////////////////////////
namespace scmi_detail {

/**
 * @brief Lock file name used for inter-process SCMI sysfs serialization.
 */
inline constexpr std::string_view kScmiProcessLockFileName = ".astl_scmi_sysfs.lock";

inline auto GetProcessLockTempDirectory(std::error_code& error_code) -> std::filesystem::path {
#if defined(ASTL_TEST_GET_TEMP_DIRECTORY_PATH)
  return ASTL_TEST_GET_TEMP_DIRECTORY_PATH(error_code);
#else
  return std::filesystem::temp_directory_path(error_code);
#endif
}

std::expected<std::filesystem::path, astl_status_code> GetDataEventDirPath(ScmiDataEventId data_event_id);

/* Parse the given text for a raw hardware tick timestamp and return alongside remaining text to parse.
 * The returned HwClockTicks contains the raw hardware tick count; MetricManager applies the
 * NativeToMonotonicRawRatio from OperationClockCorrelation to convert to nanoseconds. */
auto ParseScmiTimeStamp(std::string_view text)
    -> std::expected<std::pair<HwClockTicks, std::string_view>, astl_status_code>;

// Expected format: "<timestamp> <value>"
auto ParseDataEventValueWithTimestamp(std::string_view data_read)
    -> std::expected<std::pair<HwClockTicks, ScmiDataEventValue>, astl_status_code>;

auto GetUniqueDataEventsIds(CollectionOperations const& operations) -> std::unordered_set<ScmiDataEventId>;

/*
 * @brief For each ScmiReadOperation in the given operations, look up its corresponding data event
 *        and copy the timestamp rate.
 *        This is needed so that when we execute a ScmiReadOperation and get a timestamp back,
 *        we know how to interpret it based on the rate at which it increments.
 */
auto UpdateSampleOperationsWithTstampRates(std::vector<ScmiDataEvent> const& data_events,
                                           CollectionOperations const&       operations) -> void;

}  // namespace scmi_detail

////////////////////////////////////////////////////////////////////////////////
// Implementations of ScmiSysfsCollector template class methods declared above
////////////////////////////////////////////////////////////////////////////////

template <typename FileInterfaceT>
ScmiSysfsCollector<FileInterfaceT>::ScmiSysfsCollector(FileInterfaceT file_interface)
    : _scmi_file_interface{std::move(file_interface)} {}

template <typename FileInterfaceT>
ScmiSysfsCollector<FileInterfaceT>::~ScmiSysfsCollector() {
  // Ensure any active or paused collection is properly stopped before releasing resources.
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    ASTL_LOG_WARNING(
        "ScmiSysfsCollector destroyed while collection is active or paused. Forcing StopCollection for cleanup.");
    StopCollection();
  }
  std::scoped_lock lock{_collection_mutex};
  RollbackConfigurationState("collector destruction");
}

/*
 * @brief Get the capabilities of this collector, including the collector type.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::GetCapabilities() const -> CollectorCapability {
  return _collector_capability;
};

/*
 * @brief Set the destination for where sampled data should be sent.
 *       This is typically the CollectorManager, but can be any IRawSampleSink.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void {
  std::scoped_lock lock{_collection_mutex};
  _raw_sample_sink = raw_sample_sink;
};

/*
 * @brief Configure the collector to collect data, but don't start sampling it yet.
 *
 * @param configuration The configuration to apply to this collector, including the set of operations to run,
 *        the interval to sample at.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::ConfigureCollection(CollectionConfiguration&& configuration)
    -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::CONFIGURED &&
      _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot reconfigure while already started
  }
  if (_collection_state == CollectionState::CONFIGURED) {
    RollbackConfigurationState("ConfigureCollection replacement");
  }
  _use_software_clock_timestamps = IsEnvVarSet(EnvVar::ASTL_SCMI_USE_SOFTWARE_CLOCK_TIMESTAMPS);
  ASTL_LOG_INFO("ScmiSysfsCollector: use_software_clock_timestamps={}", _use_software_clock_timestamps);
  auto rollback_configuration_state = [this]() { RollbackConfigurationState("ConfigureCollection failure"); };

  // enable the telemetry subsystem
  auto result = ScmiWrite(std::filesystem::path{kScmiTlmEnableFileName}, kScmiTlmEnableValue);
  if (result != ASTL_STATUS_SUCCESS) {
    if (result == ASTL_STATUS_COLLECTION_ALREADY_RUNNING) {
      ASTL_LOG_ERROR("SCMI sysfs is already in use by another process");
    } else {
      ASTL_LOG_CRITICAL("Error {} enabling SCMI Telemetry subsystem!", astl::to_string(result));
    }
    rollback_configuration_state();
    return result;
  }

  _configuration          = std::move(configuration);
  _collection_state       = CollectionState::CONFIGURED;
  auto all_data_event_ids = scmi_detail::GetUniqueDataEventsIds(_configuration->Operations());
  auto data_events        = EnableDataEvents(all_data_event_ids);
  if (!data_events) {
    rollback_configuration_state();
    return data_events.error();
  }
  _data_events = *data_events;  // keep track of which data events were enabled during configuration

  // copy the tstamp rate for each data event into its corresponding Read operation
  // so we know how to interpret timestamps on sample
  scmi_detail::UpdateSampleOperationsWithTstampRates(_data_events, _configuration->Operations());

  // log some version info
  std::string de_implementation_version;
  result =
      _scmi_file_interface.Read(std::filesystem::path{kScmiDeImplementationVersionFileName}, de_implementation_version);
  ASTL_LOG_INFO("de_implementation_version: {}",
                result == ASTL_STATUS_SUCCESS ? de_implementation_version : astl::to_string(result));
  std::string telemetry_protocol_version;
  result = _scmi_file_interface.Read(std::filesystem::path{kScmiVersion}, telemetry_protocol_version);
  ASTL_LOG_INFO("version: {}", result == ASTL_STATUS_SUCCESS ? telemetry_protocol_version : astl::to_string(result));

  result = ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
  if (result != ASTL_STATUS_SUCCESS) {
    rollback_configuration_state();
    return result;
  }
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::StartCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  auto             result = ASTL_STATUS_SUCCESS;
  if (_collection_state == CollectionState::STARTED) {
    result = ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  } else if ((_collection_state != CollectionState::CONFIGURED && _collection_state != CollectionState::STOPPED) ||
             !_configuration.has_value()) {
    result = ASTL_STATUS_BAD_CONFIGURATION;  // Cannot start while already started or unconfigured
  } else {
    // Clear previous timestamps from any previous collection cycles
    _previous_timestamps.clear();

    result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStart);
    if (result == ASTL_STATUS_SUCCESS) {
      switch (_configuration->CollectionParams().collection_mode) {
        case ASTL_COLLECTION_MODE_IMMEDIATE:
          // Immediate mode does not require any special setup, we will collect data on ReadImmediate calls
          break;
        case ASTL_COLLECTION_MODE_SNAPSHOT:
          // Snapshot mode samples data on Start and Stop calls
          result = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
          break;
        case ASTL_COLLECTION_MODE_SAMPLING:
          // Sampling mode requires setting up a timer or similar mechanism to periodically collect data
          result = StartIntervalSampling();
          break;
        default:
          result = ASTL_STATUS_BAD_CONFIGURATION;  // Unsupported collection mode
          break;
      }
    }
    if (result == ASTL_STATUS_SUCCESS) {
      _collection_state = CollectionState::STARTED;
    }
  }
  return result;
}

/*
 * @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::PauseCollection() {
  std::scoped_lock lock{_collection_mutex};
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("PauseCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Pause();
  }
  if (_collection_state == CollectionState::STARTED) {
    _collection_state = CollectionState::PAUSED;
  }
  auto pause_timestamp = ClockMonotonicRaw::now();
  return EmitPauseResumeSample(PauseResumeMarker::PAUSE, pause_timestamp);
};

/*
 * @brief Resume the collection of data, starting any async tasks
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ResumeCollection() {
  std::scoped_lock lock{_collection_mutex};
  // Emit the resume marker before restarting the periodic sampler so the marker timestamp
  // strictly precedes any new samples produced after the sampler resumes.
  auto       resume_timestamp = ClockMonotonicRaw::now();
  const auto emit_status      = EmitPauseResumeSample(PauseResumeMarker::RESUME, resume_timestamp);
  if (!_periodic_sampler) {
    ASTL_LOG_WARNING("ResumeCollection called when no periodic sampler initialized");
  } else {
    _periodic_sampler->Resume();
  }
  if (_collection_state == CollectionState::PAUSED) {
    _collection_state = CollectionState::STARTED;
  }
  return emit_status;
};

/*
 * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::StopCollection() {
  // before we modify any of the collection configuration state, try to stop any
  // configured interval sampling. taking the _collection_mutex lock here has contention
  // against the sampling thread, so signal the stop _BEFORE_ grabbing _collection_mutex.
  StopIntervalSampling();
  auto             result = ASTL_STATUS_SUCCESS;
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STOPPED) {
    return ASTL_STATUS_COLLECTION_ALREADY_STOPPED;  // stop is idempotent
  }
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot stop while not started, paused, or unconfigured
  }
  switch (_configuration->CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      // Immediate mode does not require any special teardown
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      // Snapshot mode samples data on Start and Stop calls
      result = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      if (result != ASTL_STATUS_SUCCESS) {
        return result;
      }
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      // NOTE - we should already have stopped this interval sampling
      // at the top of this function before grabbing _collection_mutex
      break;
    default:
      return ASTL_STATUS_BAD_CONFIGURATION;  // Unsupported collection mode
  }
  result = ExecuteCollectionOperations(_configuration->Operations().operationsAtStop);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;  // Propagate the error code from the operation
  }
  // Restore the original enabled state of data events
  result = RestoreDataEventEnabledState(_data_events);
  _data_events.clear();
  _previous_timestamps.clear();  // Clear previous timestamps for next collection cycle
  _collection_state = CollectionState::STOPPED;
  ReleaseProcessLock();
  return result;
}

/*
 * @brief Collect a single sample of all the configured metrics.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::ReadImmediate() {
  std::scoped_lock lock{_collection_mutex};
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::CONFIGURED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;  // Cannot read while unconfigured or stopped
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

/*
 * @brief Snapshot CLOCK_MONOTONIC_RAW and the SCMI hardware counter for every sample operation.
 *        Each SCMI data event (DE_ID) can have an independent hardware clock source, so the
 *        correlation is recorded per OperationId rather than per target.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::GetNativeClockSnapshot()
    -> std::expected<ClockCorrelationMap, astl_status_code> {
  std::scoped_lock lock{_collection_mutex};
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("ScmiSysfsCollector::GetNativeClockSnapshot called without configuration; returning empty map");
    return {};
  }
  ClockCorrelationMap result;
  for (const auto& operation_ptr : _configuration->Operations().operationsOnSample) {
    const auto* scmi_op = dynamic_cast<const ScmiReadOperation*>(operation_ptr.get());
    if (!scmi_op) {
      continue;
    }
    if (_use_software_clock_timestamps) {
      const auto raw_now       = ClockMonotonicRaw::now();
      const auto native_anchor = static_cast<HwClockTicks>(raw_now.time_since_epoch().count());
      result[scmi_op->GetId()] = OperationClockCorrelation{
          raw_now, native_anchor, NativeToMonotonicRawRatio{1, 1}
      };
      continue;
    }
    auto data_event_dir_path = scmi_detail::GetDataEventDirPath(scmi_op->scmi_data_event_id);
    if (!data_event_dir_path) {
      ASTL_LOG_WARNING(
          "ScmiSysfsCollector::GetNativeClockSnapshot: could not resolve path for DE_ID {:04X}, using zero anchor",
          scmi_op->scmi_data_event_id);
      const auto raw_ratio     = NativeToMonotonicRawRatio{1'000'000LL, static_cast<intmax_t>(scmi_op->tstamp_rate)};
      result[scmi_op->GetId()] = OperationClockCorrelation{ClockMonotonicRaw::now(), HwClockTicks{0}, raw_ratio};
      continue;
    }
    std::string data_read;
    auto        read_result = _scmi_file_interface.Read(*data_event_dir_path / kScmiDataEventValueFileName, data_read);
    // Take CLOCK_MONOTONIC_RAW immediately after the sysfs read to minimise inter-clock drift
    auto raw_now = ClockMonotonicRaw::now();
    if (read_result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("ScmiSysfsCollector::GetNativeClockSnapshot: sysfs read failed for DE_ID {:04X}, returning error",
                     scmi_op->scmi_data_event_id);
      return std::unexpected{read_result};
    }
    auto parsed = scmi_detail::ParseScmiTimeStamp(data_read);
    if (!parsed) {
      ASTL_LOG_ERROR(
          "ScmiSysfsCollector::GetNativeClockSnapshot: timestamp parse failed for DE_ID {:04X}, returning error",
          scmi_op->scmi_data_event_id);
      return std::unexpected{ASTL_STATUS_FILE_ERROR};
    }
    const auto raw_ratio     = NativeToMonotonicRawRatio{1'000'000LL, static_cast<intmax_t>(scmi_op->tstamp_rate)};
    result[scmi_op->GetId()] = OperationClockCorrelation{raw_now, parsed->first, raw_ratio};
  }
  return result;
}

/* Enable a data event, return true if it was originally enabled */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::EnableDataEvent(std::filesystem::path const& data_event_dir_path)
    -> std::expected<bool, astl_status_code> {
  const auto enable_file_path = data_event_dir_path / kScmiDataEventEnableFileName;
  // check to see if the data event is already enabled (if so, we don't disable it at the end of collection)
  std::string enabled_text;
  auto        result = _scmi_file_interface.Read(enable_file_path, enabled_text);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to read enable file for data event: {} with error: {}",
                   data_event_dir_path.filename().string(), result);
    return std::unexpected{result};
  }
  const bool originally_enabled = (enabled_text == kScmiDataEventEnableValue);
  // enable the data event if it's not already enabled.
  if (!originally_enabled) {
    result = ScmiWrite(enable_file_path, kScmiDataEventEnableValue);
    if (result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to enable data event: {} with error: {}", data_event_dir_path.filename().string(), result);
      return std::unexpected{result};
    }
  }
  return originally_enabled;
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::EnableTimestamp(std::filesystem::path const& data_event_to_configure)
    -> std::expected<std::optional<bool>, astl_status_code> {
  // check to see if timestamps are already enabled for this data event
  std::optional<bool> timestamp_enabled;  // if 'none', there is no timestamp enable file for this event
  const auto          tstamp_enable_file_path = data_event_to_configure / kScmiDataEventTstampEnableFileName;
  if (!_scmi_file_interface.IsValid(tstamp_enable_file_path).value_or(false)) {
    ASTL_LOG_DEBUG("No timestamp enable file for data event: {}, timestamps will be disabled for this event",
                   data_event_to_configure.filename().string());
    return timestamp_enabled;  // return 'none' to indicate no timestamp enable file exists for this event
  }
  // there's a tstamp_enable file, try to read the original value to determine
  // if we need to restore it at the end of collection, and enable it if it's not already enabled
  std::string tstamp_enabled_text;
  auto        result = _scmi_file_interface.Read(tstamp_enable_file_path, tstamp_enabled_text);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to read timestamp enable file for data event: {} with error: {}",
                   data_event_to_configure.filename().string(), result);
    return std::unexpected{result};
  }
  timestamp_enabled = (tstamp_enabled_text == kScmiDataEventTstampEnableValue);
  // enable the timestamp if it's not already enabled, but the enable file exists
  if (!timestamp_enabled.value_or(false)) {
    result = ScmiWrite(tstamp_enable_file_path, kScmiDataEventTstampEnableValue);
    if (result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_ERROR("Failed to enable timestamp for data event: {} with error: {}",
                     data_event_to_configure.filename().string(), result);
      return std::unexpected{result};
    }
  }
  return timestamp_enabled;
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::ReadTimestampRate(std::filesystem::path const& data_event_to_configure)
    -> std::expected<std::optional<kilohertz>, astl_status_code> {
  // read the 'tstamp_rate' file to determine the rate in KHz that the data event's timestamp increments
  const auto tstamp_rate_file_path = data_event_to_configure / kScmiDataEventTstampRateFileName;
  if (!_scmi_file_interface.IsValid(tstamp_rate_file_path).value_or(false)) {
    ASTL_LOG_DEBUG("No timestamp rate file for data event: {}. Using default rate",
                   data_event_to_configure.filename().string());
    return std::nullopt;  // not an error, but no timestamp rate file exists
  }
  std::string tstamp_rate_text;
  auto        result = _scmi_file_interface.Read(tstamp_rate_file_path, tstamp_rate_text);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to read timestamp rate file for data event: {} with error: {}",
                   data_event_to_configure.filename().string(), result);
    return std::unexpected{result};
  }
  uint32_t tstamp_rate_khz{};
  auto     parse_result = std::from_chars(
      tstamp_rate_text.data(), std::next(tstamp_rate_text.data(), static_cast<ptrdiff_t>(tstamp_rate_text.size())),
      tstamp_rate_khz);
  if (parse_result.ec != std::errc()) {
    ASTL_LOG_ERROR("Failed to parse timestamp rate for data event: {} with error: {}, text was: {}",
                   data_event_to_configure.filename().string(), std::make_error_code(parse_result.ec).message(),
                   tstamp_rate_text);
    return std::unexpected{ASTL_STATUS_FILE_ERROR};
  }
  if (tstamp_rate_khz == 0) {
    ASTL_LOG_ERROR("Timestamp rate for data event: {} cannot be 0, text was: {}",
                   data_event_to_configure.filename().string(), tstamp_rate_text);
    return std::unexpected{ASTL_STATUS_BAD_CONFIGURATION};
  }
  return kilohertz{tstamp_rate_khz};
}

/*
 * @brief Enable the given data event ids, returning the set of DataEvents and their initial enable state
 */
template <typename FileInterfaceT>
std::expected<std::vector<ScmiDataEvent>, astl_status_code> ScmiSysfsCollector<FileInterfaceT>::EnableDataEvents(
    std::unordered_set<ScmiDataEventId> const& data_events_to_enable) {
  std::vector<ScmiDataEvent> enabled_data_events;
  for (const auto& data_event_id : data_events_to_enable) {
    const auto expected_data_event_dir_path = scmi_detail::GetDataEventDirPath(data_event_id);
    if (!expected_data_event_dir_path) {
      ASTL_LOG_WARNING("Data Event directory path for ID: {:08X} is not implemented", data_event_id);
      continue;
    }
    // enable the data event
    const auto data_event_dir_path         = expected_data_event_dir_path.value();
    const auto expected_originally_enabled = EnableDataEvent(data_event_dir_path);
    if (!expected_originally_enabled) {
      static_cast<void>(RestoreDataEventEnabledState(enabled_data_events));
      return std::unexpected{expected_originally_enabled.error()};
    }
    const auto originally_enabled = expected_originally_enabled.value();
    enabled_data_events.emplace_back(data_event_id, originally_enabled, std::nullopt, std::nullopt);
    auto& configured_data_event = enabled_data_events.back();
    if (!_use_software_clock_timestamps) {
      // enable the timestamp collection and determine the clock rate
      const auto expected_timestamp_enabled = EnableTimestamp(data_event_dir_path);
      if (!expected_timestamp_enabled) {
        static_cast<void>(RestoreDataEventEnabledState(enabled_data_events));
        return std::unexpected{expected_timestamp_enabled.error()};
      }
      const auto timestamp_enabled            = expected_timestamp_enabled.value();
      configured_data_event.timestamp_enabled = timestamp_enabled;

      const auto expected_timestamp_rate = ReadTimestampRate(data_event_dir_path);
      if (!expected_timestamp_rate) {
        static_cast<void>(RestoreDataEventEnabledState(enabled_data_events));
        return std::unexpected{expected_timestamp_rate.error()};
      }
      configured_data_event.timestamp_rate = expected_timestamp_rate.value();
    }
  }
  return enabled_data_events;
}

/*
 * @brief Restore the original 'enable' value, disabling all data events that were originally disabled
 *        Also restores the tstamp_enable file
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::RestoreDataEventEnabledState(std::vector<ScmiDataEvent> const& data_events)
    -> astl_status_code {
  for (const auto& data_event : data_events) {
    auto data_event_dir_path = scmi_detail::GetDataEventDirPath(data_event.id);
    if (!data_event_dir_path) {
      ASTL_LOG_ERROR("Failed to get data event directory path for ID: {}", data_event.id);
      return ASTL_STATUS_FILE_OPEN_FAILED;  // Return the error code from GetDataEventDirPath
    }
    // disable in reverse order: first disable timestamp, then event
    if (!data_event.timestamp_enabled.value_or(true)) {
      // if the timestamp for this event had a enable file and wasn't enabled originally, disable it again now.
      auto result =
          ScmiWrite(data_event_dir_path.value() / kScmiDataEventTstampEnableFileName, kScmiDataEventTstampDisableValue);
      if (result != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to disable timestamp for data event ID: {:04X} with error: {}", data_event.id, result);
        return result;  // Return the error code from Write
      }
    }
    if (!data_event.originally_enabled) {
      auto result = ScmiWrite(data_event_dir_path.value() / kScmiDataEventEnableFileName, kScmiDataEventDisableValue);
      if (result != ASTL_STATUS_SUCCESS) {
        ASTL_LOG_ERROR("Failed to disable data event ID: {:04X} with error: {}", data_event.id, result);
        return result;  // Return the error code from Write
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Casts a sequence of abstract operations to ScmiReadOperation and executes them.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::ExecuteCollectionOperations(OperationSequence const& operations)
    -> astl_status_code {
  for (const auto& operation_ptr : operations) {
    const auto* scmi_operation = dynamic_cast<ScmiReadOperation*>(operation_ptr.get());
    if (!scmi_operation) {
      return ASTL_STATUS_BAD_ARGUMENT;  // Invalid operation type
    }
    auto result = ExecuteScmiReadOperation(*scmi_operation);
    if (result != ASTL_STATUS_SUCCESS) {
      return result;  // Propagate the error code from the operation
    }
  }
  return ASTL_STATUS_SUCCESS;
}

/*
 * @brief Executes a single ScmiReadOperation
 *        This will handle the actual file operations in the sysfs SCMI directory.
 *        creates a new RawSampledData object from the read value
 *        and sends it to the sample sink.
 */
template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::ExecuteScmiReadOperation(ScmiReadOperation const& operation)
    -> astl_status_code {
  auto data_event_dir_path = scmi_detail::GetDataEventDirPath(operation.scmi_data_event_id);
  if (!data_event_dir_path) {
    ASTL_LOG_CRITICAL("Error {} getting dir path for SCMI read operation for data event ID: {:04X}",
                      astl::to_string(data_event_dir_path.error()), operation.scmi_data_event_id);
    return data_event_dir_path.error();  // Return the error code from GetDataEventDirPath
  }
  ASTL_LOG_TRACE("Executing SCMI read operation for data event ID: {:04X}", operation.scmi_data_event_id);
  std::string data_read;
  auto        result = _scmi_file_interface.Read(data_event_dir_path.value() / kScmiDataEventValueFileName, data_read);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_CRITICAL("Error {} executing SCMI read operation for data event ID: {:04X}", astlStatusString(result),
                      operation.scmi_data_event_id);
    return result;
  }
  // TODO(https://github.com/Arm-Debug/ASTL/issues/92) - potentially disable timestamps depending on chosen
  // optimization flags
  auto parsed_value = scmi_detail::ParseDataEventValueWithTimestamp(data_read);
  if (!parsed_value) {
    return parsed_value.error();
  }
  const auto timestamp = _use_software_clock_timestamps
                             ? static_cast<HwClockTicks>(ClockMonotonicRaw::now().time_since_epoch().count())
                             : parsed_value->first;
  auto       raw_value = AstlValue{parsed_value->second.value};

  /**
   * @brief Discard samples that arrive with the same timestamp as the previous one.
   * @todo ASTL-135: Evaluate event-driven SCMI sampling—subscribe to driver “Samples Ready” notifications
   *       instead of periodic polling to avoid duplicates.
   */
  auto prev_timestamp_it = _previous_timestamps.find(operation.scmi_data_event_id);
  if (prev_timestamp_it != _previous_timestamps.end() && prev_timestamp_it->second == timestamp) {
    ASTL_LOG_WARNING(
        "ScmiSysfsCollector: discarding sample with duplicate timestamp for data event ID: {:04X}, timestamp: {}",
        operation.scmi_data_event_id, timestamp);
    return ASTL_STATUS_SUCCESS;  // Discard sample but return success
  }

  // Update the previous timestamp for this data event
  _previous_timestamps[operation.scmi_data_event_id] = timestamp;

  RawSampledData raw_sampled_data{operation.GetId(), raw_value, timestamp};

  if (_raw_sample_sink) {
    return _raw_sample_sink->SinkRawSamples(_configuration->Target(), {&raw_sampled_data, 1});
  }

  return ASTL_STATUS_SUCCESS;  // Successfully read and sent the sample
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::CheckStartIntervalSampling() const -> astl_status_code {
  return CheckPeriodicSamplerStart(_collection_state, _configuration,
                                   {CollectionState::CONFIGURED, CollectionState::STOPPED, CollectionState::PAUSED},
                                   "SCMI interval sampling");
}

/*
 * @brief Initialize any threads or async tasks needed for interval sampling.
 */
template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::StartIntervalSampling() {
  auto status = CheckStartIntervalSampling();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  return StartPeriodicSamplerForCollector(this, *_configuration, _periodic_sampler, "SCMI interval sampling");
}

template <typename FileInterfaceT>
astl_status_code ScmiSysfsCollector<FileInterfaceT>::EmitPauseResumeSample(PauseResumeMarker        marker_type,
                                                                           ProcessedSampleTimestamp timestamp) {
  if (!_configuration.has_value()) {
    ASTL_LOG_WARNING("Marker sample emission skipped because collector has no active configuration");
    return ASTL_STATUS_SUCCESS;
  }
  if (_raw_sample_sink == nullptr) {
    return ASTL_STATUS_SUCCESS;
  }
  auto marker = (marker_type == PauseResumeMarker::PAUSE) ? RawSampledData::PauseMarker(timestamp)
                                                          : RawSampledData::ResumeMarker(timestamp);
  return _raw_sample_sink->SinkRawSamples(_configuration->Target(), {&marker, 1});
}

/*
 * @brief Stop any background threads or async tasks that were started for interval sampling.
 *
 * @returns ASTL_STATUS_SUCCESS even if there was no ongoing task. this is because
 *          we must try to signal _stop_sampling_task in a lightweight way with no lock
 */
template <typename FileInterfaceT>
void ScmiSysfsCollector<FileInterfaceT>::StopIntervalSampling() {
  _periodic_sampler = nullptr;  // destroy periodic_sampler and wait for its thread pool to empty
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::ScmiWrite(std::filesystem::path const& path, std::string_view value)
    -> astl_status_code {
  std::string current_value;
  auto        result = _scmi_file_interface.Read(path, current_value);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }
  if (current_value == value) {
    return ASTL_STATUS_SUCCESS;
  }

  const auto write_permission = _scmi_file_interface.HasWritePermission(path);
  if (!write_permission) {
    return write_permission.error();
  }
  if (!write_permission.value()) {
    ASTL_LOG_WARNING("SCMI sysfs file '{}' is not writable; skipping write of '{}'", path.generic_string(), value);
    return ASTL_STATUS_SUCCESS;
  }

  result = AcquireProcessLock();
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }

  return _scmi_file_interface.Write(path, value);
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::GetSharedProcessLockState() -> SharedProcessLockState& {
  static SharedProcessLockState state;
  return state;
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::AcquireProcessLock() -> astl_status_code {
  if (_holds_process_lock_ref) {
    return ASTL_STATUS_SUCCESS;
  }
  auto&            shared_state = GetSharedProcessLockState();
  std::scoped_lock shared_lock{shared_state.mutex};
  if (shared_state.lock && shared_state.lock->IsLocked()) {
    ++shared_state.refcount;
    _holds_process_lock_ref = true;
    return ASTL_STATUS_SUCCESS;
  }
  std::error_code error_code;
  const auto      temp_dir = scmi_detail::GetProcessLockTempDirectory(error_code);
  if (error_code) {
    ASTL_LOG_ERROR("Failed to get temporary directory path for SCMI process lock: {}", error_code.message());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  const auto process_lock_file_path = temp_dir / std::string{scmi_detail::kScmiProcessLockFileName};
  shared_state.lock =
      std::make_unique<FilesystemProcessLock<FileInterfaceT>>(_scmi_file_interface, process_lock_file_path.string());
  const auto status = shared_state.lock->Status();
  if (status != ASTL_STATUS_SUCCESS) {
    shared_state.lock.reset();
    shared_state.refcount = 0;
    return status;
  }
  shared_state.refcount   = 1;
  _holds_process_lock_ref = true;
  return ASTL_STATUS_SUCCESS;
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::ReleaseProcessLock() noexcept -> void {
  if (!_holds_process_lock_ref) {
    return;
  }
  auto&            shared_state = GetSharedProcessLockState();
  std::scoped_lock shared_lock{shared_state.mutex};
  _holds_process_lock_ref = false;
  if (shared_state.refcount > 0) {
    --shared_state.refcount;
  } else {
    ASTL_LOG_ERROR(
        "ScmiSysfsCollector: process lock refcount underflow (logic error): "
        "_holds_process_lock_ref was true but refcount is already 0");
  }
  if (shared_state.refcount == 0 && shared_state.lock) {
    shared_state.lock->Release();
    shared_state.lock.reset();
  }
}

template <typename FileInterfaceT>
auto ScmiSysfsCollector<FileInterfaceT>::RollbackConfigurationState(const char* rollback_context) noexcept -> void {
  if (!_data_events.empty()) {
    auto restore_result = RestoreDataEventEnabledState(_data_events);
    if (restore_result != ASTL_STATUS_SUCCESS) {
      ASTL_LOG_WARNING("Failed to roll back SCMI data event state after {}: {}", rollback_context,
                       astl::to_string(restore_result));
    }
    _data_events.clear();
  }
  _previous_timestamps.clear();
  _configuration.reset();
  _collection_state = CollectionState::UNCONFIGURED;
  ReleaseProcessLock();
}

}  // namespace astl

#endif  // SCMI_SYSFS_COLLECTOR_HPP_
