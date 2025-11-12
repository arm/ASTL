#ifndef INCLUDE_PROTOCOL_TELEMETRY_HPP
#define INCLUDE_PROTOCOL_TELEMETRY_HPP

#include <sys/types.h>

#include <chrono>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "common.hpp"
#include "fsnode.hpp"

namespace mock_sysfs {

using update_interval_t = std::pair<std::chrono::seconds, int16_t>;

constexpr int kHexRadix = 16;

enum class TelemetryFile {
  ALL_DES_ENABLE,
  ALL_DES_TSTAMP_ENABLE,
  CURRENT_UPDATE_INTERVAL_MS,
  DES_BULK_READ,
  DES_SINGLE_SAMPLE_READ,
  TLM_ENABLE,
  VERSION,
  DE_IMPLEMENTATION_VERSION,
  AVAILABLE_UPDATE_INTERVALS_MS,
  INTERVALS_DISCRETE,
  ENABLE,
  COMPO_INSTANCE_ID,
  COMPO_TYPE,
  INSTANCE_ID,
  PERSISTENT,
  TSTAMP_EXP,
  TYPE,
  UNIT,
  UNIT_EXP,
  TSTAMP_ENABLE,
  VALUE,
  COMPOSING_DES,
  UNKNOWN
};

/**
 * @brief Initializes the telemetry protocol.
 *
 * @details This function creates the SCMI telemetry target instance, starts the data event generators,
 * and then calls construction of the telemetry file tree @ref InitProtocolTelemetryFileTree.
 *
 * TODO(ASTL-13): Discover config vector to dynamically build DE structure.
 *
 *
 * @param g_root Pointer to the root file system node under which the telemetry directory will be created.
 * @return std::unique_ptr<FileSystemNode> Unique pointer to the telemetry directory node.
 */
std::unique_ptr<FileSystemNode> InitProtocolTelemetry(FileSystemNode* g_root);

/**
 * @brief Writes telemetry data for the specified node.
 *
 * @param node File system node for telemetry operations.
 * @param value String value to write.
 * @return ErrorCode Result of the write operation.
 */
ErrorCode HandleProtocolTelemetryWrite(const FileSystemNode* node, const std::string& value);

/**
 * @brief Reads telemetry data from the specified node.
 *
 * Skips context-based reads for write-only files (e.g., ALL_DES_ENABLE, ALL_DES_TSTAMP_ENABLE),
 * and for fields in the "info" directory (e.g., COMPO_INSTANCE_ID, COMPO_TYPE) which are immutable
 * after discovery time.
 *
 * Retains context-based getters only for fields that may change during runtime.
 *
 * @param node File system node for telemetry operations.
 * @return std::string Read data as a string.
 */
std::string HandleProtocolTelemetryRead(const FileSystemNode* node);

struct UpdateInterval {
  bool                           discrete{false};
  std::vector<update_interval_t> update_intervals;
  update_interval_t              step_size{std::chrono::seconds{1}, 0};
  update_interval_t              active_update_interval{std::chrono::seconds{1}, 0};
};

struct DesGroup {
  DesGroup(group_id_t group_id, bool enable, bool tstamp_enable, UpdateInterval intervals)
      : group_id_(group_id), enable_(enable), tstamp_enable_(tstamp_enable), intervals_(intervals) {}

  group_id_t                   group_id_{};
  std::vector<data_event_id_t> des_;
  bool                         enable_{false};
  bool                         tstamp_enable_{false};
  UpdateInterval               intervals_{};
};

/**
 * @brief Abstract base class for telemetry data events.
 *
 * This struct serves as a template for creating child data event classes.
 * It encapsulates common properties and defines the interface for generating telemetry data.
 * Users should derive from this class and implement their own Generate() function to produce
 * event-specific output.
 */
class DataEvent {
 public:
  DataEvent(const DataEvent&)            = delete;
  DataEvent(DataEvent&&)                 = delete;
  DataEvent& operator=(const DataEvent&) = delete;
  DataEvent& operator=(DataEvent&&)      = delete;
  DataEvent(data_event_id_t data_event_id, bool enable, bool tstamp_enable, _astl_value_t last_value,
            _astl_value_type_t value_type, std::chrono::system_clock::time_point last_timestamp,
            uint32_t compo_instance_id, uint32_t compo_type, uint32_t instance_id, bool persistent, bool tstamp_exp,
            uint32_t type, std::string unit, std::string unit_exp, std::optional<group_id_t> group_id = std::nullopt)
      : id_(data_event_id),
        enable_(enable),
        tstamp_enable_(tstamp_enable),
        last_value_(last_value),
        value_type_(value_type),
        last_timestamp_(last_timestamp),
        compo_instance_id_(compo_instance_id),
        compo_type_(compo_type),
        instance_id_(instance_id),
        persistent_(persistent),
        tstamp_exp_(tstamp_exp),
        type_(type),
        unit_(std::move(unit)),
        unit_exp_(std::move(unit_exp)),
        group_id_(group_id) {}
  virtual ~DataEvent() = default;

  virtual astl_value_t Generate() = 0;

  data_event_id_t                       id_;
  bool                                  enable_;
  bool                                  tstamp_enable_;
  astl_value_t                          last_value_;
  astl_value_type_t                     value_type_;
  std::chrono::system_clock::time_point last_timestamp_;

  uint32_t    compo_instance_id_;
  uint32_t    compo_type_;
  uint32_t    instance_id_;
  bool        persistent_;
  bool        tstamp_exp_;
  uint32_t    type_;
  std::string unit_;
  std::string unit_exp_;

  std::optional<group_id_t> group_id_;
};

/**
 * @brief Class managing an SCMI target.
 *
 * This class provides configuration and operational control for SCMI telemetry.
 * It maintains telemetry settings, handles data event management, and offers
 * methods to initialize and access telemetry data.
 *
 * Set default config in config_protocol_telemetry.hpp
 */
class SCMITelemetryTarget {
 public:
  SCMITelemetryTarget() = default;
  SCMITelemetryTarget(std::string const& tlm_id, bool all_des_enable, bool all_des_tstamp_enable,
                      UpdateInterval intervals, bool tlm_enable, std::string version,
                      std::string de_implementation_version, std::vector<std::unique_ptr<DataEvent>> data_events);

  ~SCMITelemetryTarget()                                     = default;
  SCMITelemetryTarget(const SCMITelemetryTarget&)            = delete;
  SCMITelemetryTarget& operator=(const SCMITelemetryTarget&) = delete;
  SCMITelemetryTarget(SCMITelemetryTarget&&)                 = delete;
  SCMITelemetryTarget& operator=(SCMITelemetryTarget&&)      = delete;

  /**
   * @brief Returns an instance of the telemetry target.
   * @return SCMITelemetryTarget& Reference to the global telemetry target.
   */
  static SCMITelemetryTarget& Instance(const std::string& tlm_id);

  /**
   * @brief Retrieves the "all data event enable" flag.
   * @return true if all descriptors are enabled; false otherwise.
   */
  bool GetAllDesEnableFlag() const { return all_des_enable_; }

  /**
   * @brief Retrieves the "all data event timestamp enable" flag.
   * @return true if timestamps for all descriptors are enabled; false otherwise.
   */
  bool GetAllDesTstampEnableFlag() const { return all_des_tstamp_enable_; }

  /**
   * @brief Gets the current update interval in milliseconds.
   * @return update_interval_t : a pair of seconds and int16_t (representing the signed power of 10 to multiply seconds
   * by).
   */
  auto GetCurrentUpdateInterval() const -> update_interval_t { return intervals_.active_update_interval; }

  /**
   * @brief Returns the global telemetry enable flag.
   * @return true if telemetry is enabled; false otherwise.
   */
  bool GetTlmEnableFlag() const { return tlm_enable_; }

  /**
   * @brief Retrieves the telemetry protocol version.
   * @return const std::string& The version string.
   */
  const std::string& GetVersion() const { return version_; }

  /**
   * @brief DE implementation version: a 128bit value printed in UUID format.
   * @return const std::string& The de implementation version string.
   */
  const std::string& GetDEImplementationVersion() const { return de_implementation_version_; }

  /**
   * @brief Gets the available update intervals in milliseconds.
   * @return Vector of available update intervals.
   */
  auto GetAvailableUpdateIntervals() const -> std::vector<update_interval_t> { return intervals_.update_intervals; }

  /**
   * @brief Checks if the available update intervals are defined as a discrete set.
   * @return true if intervals are discrete; false if defined by a range.
   */
  bool GetIntervalsAreDiscreteFlag() const { return intervals_.discrete; }

  /**
   * @brief Retrieves the step size between update intervals.
   * @return uint32_t The step size.
   */
  auto GetStepSize() const -> update_interval_t { return intervals_.step_size; }

  /**
   * @brief Returns the container of telemetry data events.
   * @return const std::vector<std::unique_ptr<DataEvent>>& Container holding unique pointers to DataEvent objects.
   */
  const std::vector<std::unique_ptr<DataEvent>>& GetDataEvents() const { return data_events_; }

  /**
   * @brief Sets the "all data event enable" flag.
   * @param enable Boolean value to enable or disable all descriptors.
   */
  void SetAllDesEnableFlag(bool enable) { all_des_enable_ = enable; }

  /**
   * @brief Sets the "all descriptor timestamp enable" flag.
   * @param enable Boolean value to enable or disable timestamps for all descriptors.
   */
  void SetAllDesTstampEnableFlag(bool enable) { all_des_tstamp_enable_ = enable; }

  /**
   * @brief Sets the current update interval in milliseconds.
   * @param interval_ms New update interval in milliseconds.
   */
  void SetCurrentUpdateInterval(const update_interval_t& interval) { intervals_.active_update_interval = interval; }

  /**
   * @brief Sets the global telemetry enable flag.
   * @param enable Boolean value to enable or disable telemetry.
   */
  void SetTlmEnableFlag(bool enable) { tlm_enable_ = enable; }

  /**
   * @brief Retrieves a telemetry data event by its identifier.
   * @param identifier The unique identifier of the data event.
   * @return DataEvent* Pointer to the corresponding data event, or nullptr if not found.
   */
  DataEvent* GetDataEventById(data_event_id_t identifier);

  std::unordered_map<group_id_t, std::unique_ptr<DesGroup>>& GetGroups() { return groups_; }

 private:
  bool           all_des_enable_{};
  bool           all_des_tstamp_enable_{};
  UpdateInterval intervals_;
  bool           tlm_enable_{};
  std::string    version_;
  std::string    de_implementation_version_;

  std::unordered_map<group_id_t, std::unique_ptr<DesGroup>> groups_;
  const std::vector<std::unique_ptr<DataEvent>>             data_events_;
};

}  // namespace mock_sysfs

#endif  // INCLUDE_PROTOCOL_TELEMETRY_HPP
