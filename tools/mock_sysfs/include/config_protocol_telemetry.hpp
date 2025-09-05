#ifndef INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP
#define INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP

#include <cstdint>
#include <random>
#include <vector>

#include "common_data_generator.hpp"
#include "protocol_telemetry.hpp"

namespace mock_sysfs {

/**
 * @brief Default driver settings for SCMI Kernel Telemetry userspace API
 * @see Linux Kernel SCMI Telemetry Support Confluence page:
 *      https://confluence.arm.com/display/CESW/Linux+Kernel+SCMI+Telemetry+Support+-+v4.0+ALPHA_0+--+WIP
 */
constexpr bool kAllDesEnable       = true;  ///< Enable all Data Events (DE) reporting at once
constexpr bool kAllDesTstampEnable = true;  ///< Enable timestamping for all Data Events (DE)
constexpr bool kTlmEnable          = true;  ///< Enable the Telemetry (TLM) subsystem globally

constexpr const char* kTelemetryVersion   = "1.0";  ///< SCMI Telemetry protocol version
constexpr const char* kDEDataEventVersion = "0xCAFEBABECAFEBABECAFEBABEBEEF0000";

const struct UpdateInterval kUpdateInterval {
  .discrete = true,
  .update_intervals_ms =
      {
          std::chrono::milliseconds{100},
          std::chrono::milliseconds{2000},
          std::chrono::milliseconds{5000},
  },
  .active_update_interval_ms = std::chrono::milliseconds{100},
};

// Groups
const struct DesGroup kDesGroup0 {
  .group_id = 0, .des = {0x0000, 0x0016}, .enable = false, .tstamp_enable = false, .intervals = kUpdateInterval,
};

const struct DesGroup kDesGroup1 {
  .group_id = 1, .des = {0x7A9B, 0x8C3D, 0x9E4F, 0x1A68}, .enable = false, .tstamp_enable = false,
  .intervals = kUpdateInterval,
};

// DEs
constexpr uint64_t kMinRand = 50;
constexpr uint64_t kMaxRand = 110;
class ExampleDataEvent : public DataEvent {
  using DataEvent::DataEvent;

  astl_value_t Generate() override {
    static std::random_device               rdev;
    static std::mt19937                     gen(rdev());
    std::uniform_int_distribution<uint64_t> dis(kMinRand, kMaxRand);
    uint64_t                                value = dis(gen);
    // TODO(ASTL-116) - Handle all other _astl_value_type_t
    last_value_.ui64 = value;  // NOLINT
    last_timestamp_  = std::chrono::system_clock::now();
    return last_value_;
  }
};

class CsvDataEvent : public DataEvent {
 public:
  CsvDataEvent(uint16_t data_event_id, bool enable, bool tstamp_enable, astl_value_t last_value,
               astl_value_type_t value_type, std::chrono::system_clock::time_point last_timestamp,
               uint32_t compo_instance_id, uint32_t compo_type, uint32_t instance_id, bool persistent, bool tstamp_exp,
               uint32_t type, std::string unit, std::string unit_exp, const std::string& csv_path, uint8_t column,
               std::optional<const DesGroup*> group = std::nullopt)
      : DataEvent(data_event_id, enable, tstamp_enable, last_value, value_type, last_timestamp, compo_instance_id,
                  compo_type, instance_id, persistent, tstamp_exp, type, std::move(unit), std::move(unit_exp), group),
        csv_gen_(csv_path, column) {}

  astl_value_t Generate() override {
    auto str = csv_gen_.GenerateCSV();
    if (!str.empty()) {
      last_value_.ui64 = std::stoull(str);
      last_timestamp_  = std::chrono::system_clock::now();
    }
    return last_value_;
  }

 private:
  CSVDataGenerator csv_gen_;
};

constexpr bool                              kExampleDataEventEnable       = false;
constexpr bool                              kExampleDataEventTstampEnable = true;
constexpr astl_value_t                      kExampleDataEventLatestValue{.ui64 = 0ULL};
constexpr astl_value_type_t                 kExampleDataEventValueType     = ASTL_VALUE_UINT64;
const std::chrono::system_clock::time_point kExampleDataEventLastTimestamp = std::chrono::system_clock::now();
constexpr bool                              kExampleDataEventPersistent    = true;
constexpr bool                              kExampleDataEventTstampExp     = false;
constexpr uint32_t                          kExampleDataEventType          = 1;
constexpr uint32_t                          kExampleDataEventGroup         = 0;

constexpr uint16_t    kExampleDataEventId1              = 0x0000;
constexpr uint32_t    kExampleDataEventCompoInstanceId1 = 101;
constexpr uint32_t    kExampleDataEventCompoType1       = 201;
constexpr uint32_t    kExampleDataEventInstanceId1      = 301;
constexpr const char* kExampleDataEventUnit1            = "Celsius";
constexpr const char* kExampleDataEventUnitExp1         = "none";

constexpr uint16_t    kExampleDataEventId2              = 0x0016;
constexpr uint32_t    kExampleDataEventCompoInstanceId2 = 102;
constexpr uint32_t    kExampleDataEventCompoType2       = 202;
constexpr uint32_t    kExampleDataEventInstanceId2      = 302;
constexpr const char* kExampleDataEventUnit2            = "Percent";
constexpr const char* kExampleDataEventUnitExp2         = "none";

constexpr uint32_t kCsvDataEventGroup = 1;
// Temperature (Celsius) event – column 1
constexpr uint16_t     kTemperatureDataEventId = 0x7A9B;
constexpr astl_value_t kTemperatureDataEventLatestValue{.ui64 = 40ULL};
constexpr uint32_t     kTemperatureDataEventCompoInstanceId = 103;
constexpr uint32_t     kTemperatureDataEventCompoType       = 203;
constexpr uint32_t     kTemperatureDataEventInstanceId      = 303;
constexpr const char*  kTemperatureDataEventUnit            = "Celsius";
constexpr const char*  kTemperatureDataEventUnitExp         = "none";

// Throttle Count (Count) event – column 2
constexpr uint16_t     kThrottleCountEventId = 0x8C3D;
constexpr astl_value_t kThrottleCountEventLatestValue{.ui64 = 5ULL};
constexpr uint32_t     kThrottleCountEventCompoInstanceId = 104;
constexpr uint32_t     kThrottleCountEventCompoType       = 204;
constexpr uint32_t     kThrottleCountEventInstanceId      = 304;
constexpr const char*  kThrottleCountEventUnit            = "Count";
constexpr const char*  kThrottleCountEventUnitExp         = "none";

// Power (Watts) event – column 3
constexpr uint16_t     kPowerEventId = 0x9E4F;
constexpr astl_value_t kPowerEventLatestValue{.ui64 = 500ULL};
constexpr uint32_t     kPowerEventCompoInstanceId = 105;
constexpr uint32_t     kPowerEventCompoType       = 205;
constexpr uint32_t     kPowerEventInstanceId      = 305;
constexpr const char*  kPowerEventUnit            = "Watts";
constexpr const char*  kPowerEventUnitExp         = "none";

// Frequency (MHz) event – column 4
constexpr uint16_t     kFreqDataEventId = 0x1A68;
constexpr astl_value_t kFreqDataEventLatestValue{.ui64 = 2000ULL};
constexpr uint32_t     kFreqDataEventCompoInstanceId = 103;
constexpr uint32_t     kFreqDataEventCompoType       = 203;
constexpr uint32_t     kFreqDataEventInstanceId      = 303;
constexpr const char*  kFreqDataEventUnit            = "MHz";
constexpr const char*  kFreqDataEventUnitExp         = "none";

inline const char* const kCsvFilePath = []() {
  const char* env = std::getenv("ASTL_MOCKSYSFS_CSV_FILE_PATH");
  return (env != nullptr) ? env : "./SoC_Throttling_Simulation.csv";
}();

inline std::vector<std::unique_ptr<DataEvent>> CreateTelemetryDataEvents() {
  std::vector<std::unique_ptr<DataEvent>> events;

  events.push_back(std::make_unique<ExampleDataEvent>(
      kExampleDataEventId1, kExampleDataEventEnable, kExampleDataEventTstampEnable, kExampleDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLastTimestamp, kExampleDataEventCompoInstanceId1,
      kExampleDataEventCompoType1, kExampleDataEventInstanceId1, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kExampleDataEventUnit1, kExampleDataEventUnitExp1,
      std::optional<const DesGroup*>{&kDesGroup0}));

  events.push_back(std::make_unique<ExampleDataEvent>(
      kExampleDataEventId2, kExampleDataEventEnable, kExampleDataEventTstampEnable, kExampleDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLastTimestamp, kExampleDataEventCompoInstanceId2,
      kExampleDataEventCompoType2, kExampleDataEventInstanceId2, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kExampleDataEventUnit2, kExampleDataEventUnitExp2,
      std::optional<const DesGroup*>{&kDesGroup0}));

  // Temperature (Celsius) event – column 1
  events.push_back(std::make_unique<CsvDataEvent>(
      kTemperatureDataEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kTemperatureDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLastTimestamp, kTemperatureDataEventCompoInstanceId,
      kTemperatureDataEventCompoType, kTemperatureDataEventInstanceId, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kTemperatureDataEventUnit, kTemperatureDataEventUnitExp,
      kCsvFilePath, 1, std::optional<const DesGroup*>{&kDesGroup1}));

  // Throttle Count (Count) event – column 2
  events.push_back(std::make_unique<CsvDataEvent>(
      kThrottleCountEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kThrottleCountEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLastTimestamp, kThrottleCountEventCompoInstanceId,
      kThrottleCountEventCompoType, kThrottleCountEventInstanceId, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kThrottleCountEventUnit, kThrottleCountEventUnitExp,
      kCsvFilePath, 2, std::optional<const DesGroup*>{&kDesGroup1}));

  // Power (Watts) event – column 3
  events.push_back(std::make_unique<CsvDataEvent>(
      kPowerEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kPowerEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLastTimestamp, kPowerEventCompoInstanceId, kPowerEventCompoType,
      kPowerEventInstanceId, kExampleDataEventPersistent, kExampleDataEventTstampExp, kExampleDataEventType,
      kPowerEventUnit, kPowerEventUnitExp, kCsvFilePath, 3, std::optional<const DesGroup*>{&kDesGroup1}));

  // Frequency (MHz) event – column 4
  events.push_back(std::make_unique<CsvDataEvent>(
      kFreqDataEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kFreqDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLastTimestamp, kFreqDataEventCompoInstanceId,
      kFreqDataEventCompoType, kFreqDataEventInstanceId, kExampleDataEventPersistent, kExampleDataEventTstampExp,
      kExampleDataEventType, kFreqDataEventUnit, kFreqDataEventUnitExp, kCsvFilePath, 4,
      std::optional<const DesGroup*>{&kDesGroup1}));

  return events;
}

}  // namespace mock_sysfs

#endif  // INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP
