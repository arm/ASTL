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
constexpr bool kAllDesEnable         = true;  ///< Enable all Data Events (DE) reporting at once
constexpr bool kAllDesTstampEnable   = true;  ///< Enable timestamping for all Data Events (DE)
constexpr bool kTlmEnable            = true;  ///< Enable the Telemetry (TLM) subsystem globally
constexpr bool kIntervalsAreDiscrete = true;  ///< True = fixed list; False = range (lowest–highest, step)

constexpr const char* kTelemetryVersion = "1.0";  ///< SCMI Telemetry protocol version

constexpr uint32_t                 kCurrentUpdateIntervalMs  = 1000;  ///< Selected sampling interval (ms)
static const std::vector<uint32_t> kAvailableUpdateIntervals = {      ///< Supported intervals (ms) in discrete mode
    1000, 2000, 5000};
static const std::vector<uint32_t> kDiscreteIntervals        = {  ///< Alias for discrete-mode intervals
    1000, 2000, 5000};

constexpr uint32_t kLowestInterval  = 1000;  ///< Min interval (ms) in continuous-range mode
constexpr uint32_t kHighestInterval = 5000;  ///< Max interval (ms) in continuous-range mode
constexpr uint32_t kStepSize        = 1000;  ///< Step size (ms) in continuous-range mode

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
    // NOLINTBEGIN
    latest_value_.ui64 = value;
    // NOLINTEND
    return latest_value_;
  }
};

class CsvDataEvent : public DataEvent {
 public:
  CsvDataEvent(uint16_t de_id, bool enable, bool tstampEnable, astl_value_t latestValue, astl_value_type_t valueType,
               uint64_t latestTimestamp, uint32_t compoInstanceId, uint32_t compoType, uint32_t instanceId,
               bool persistent, bool tstampExp, uint32_t type, const char* unit, const char* unitExp,
               const std::string& csv_path, uint8_t column)
      : DataEvent(de_id, enable, tstampEnable, latestValue, valueType, latestTimestamp, compoInstanceId, compoType,
                  instanceId, persistent, tstampExp, type, unit, unitExp),
        csv_gen_(csv_path, column) {}

  astl_value_t Generate() override {
    auto str = csv_gen_.GenerateCSV();
    if (!str.empty()) {
      latest_value_.ui64 = std::stoull(str);
    }
    return latest_value_;
  }

 private:
  CSVDataGenerator csv_gen_;
};

constexpr bool              kExampleDataEventEnable       = false;
constexpr bool              kExampleDataEventTstampEnable = true;
constexpr astl_value_t      kExampleDataEventLatestValue{.ui64 = 0ULL};
constexpr astl_value_type_t kExampleDataEventValueType       = ASTL_VALUE_UINT64;
constexpr uint64_t          kExampleDataEventLatestTimestamp = 1678901234000ULL;
constexpr bool              kExampleDataEventPersistent      = true;
constexpr bool              kExampleDataEventTstampExp       = false;
constexpr uint32_t          kExampleDataEventType            = 1;

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
      kExampleDataEventValueType, kExampleDataEventLatestTimestamp, kExampleDataEventCompoInstanceId1,
      kExampleDataEventCompoType1, kExampleDataEventInstanceId1, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kExampleDataEventUnit1, kExampleDataEventUnitExp1));

  events.push_back(std::make_unique<ExampleDataEvent>(
      kExampleDataEventId2, kExampleDataEventEnable, kExampleDataEventTstampEnable, kExampleDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLatestTimestamp, kExampleDataEventCompoInstanceId2,
      kExampleDataEventCompoType2, kExampleDataEventInstanceId2, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kExampleDataEventUnit2, kExampleDataEventUnitExp2));

  // Temperature (Celsius) event – column 1
  events.push_back(std::make_unique<CsvDataEvent>(
      kTemperatureDataEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kTemperatureDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLatestTimestamp, kTemperatureDataEventCompoInstanceId,
      kTemperatureDataEventCompoType, kTemperatureDataEventInstanceId, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kTemperatureDataEventUnit, kTemperatureDataEventUnitExp,
      kCsvFilePath, 1));

  // Throttle Count (Count) event – column 2
  events.push_back(std::make_unique<CsvDataEvent>(
      kThrottleCountEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kThrottleCountEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLatestTimestamp, kThrottleCountEventCompoInstanceId,
      kThrottleCountEventCompoType, kThrottleCountEventInstanceId, kExampleDataEventPersistent,
      kExampleDataEventTstampExp, kExampleDataEventType, kThrottleCountEventUnit, kThrottleCountEventUnitExp,
      kCsvFilePath, 2));

  // Power (Watts) event – column 3
  events.push_back(std::make_unique<CsvDataEvent>(
      kPowerEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kPowerEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLatestTimestamp, kPowerEventCompoInstanceId, kPowerEventCompoType,
      kPowerEventInstanceId, kExampleDataEventPersistent, kExampleDataEventTstampExp, kExampleDataEventType,
      kPowerEventUnit, kPowerEventUnitExp, kCsvFilePath, 3));

  // Frequency (MHz) event – column 4
  events.push_back(std::make_unique<CsvDataEvent>(
      kFreqDataEventId, kExampleDataEventEnable, kExampleDataEventTstampEnable, kFreqDataEventLatestValue,
      kExampleDataEventValueType, kExampleDataEventLatestTimestamp, kFreqDataEventCompoInstanceId,
      kFreqDataEventCompoType, kFreqDataEventInstanceId, kExampleDataEventPersistent, kExampleDataEventTstampExp,
      kExampleDataEventType, kFreqDataEventUnit, kFreqDataEventUnitExp, kCsvFilePath, 4));

  return events;
}

}  // namespace mock_sysfs

#endif  // INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP
