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

// Constants for the first ExampleDataEvent
constexpr uint16_t          kExampleDataEventId1           = 0x0000;
constexpr bool              kExampleDataEventEnable1       = false;
constexpr bool              kExampleDataEventTstampEnable1 = true;
constexpr astl_value_t      kExampleDataEventLatestValue1{.ui64 = 50ULL};
constexpr astl_value_type_t kExampleDataEventValueType1       = ASTL_VALUE_UINT64;
constexpr uint64_t          kExampleDataEventLatestTimestamp1 = 1678901234000ULL;
constexpr uint32_t          kExampleDataEventCompoInstanceId1 = 101;
constexpr uint32_t          kExampleDataEventCompoType1       = 201;
constexpr uint32_t          kExampleDataEventInstanceId1      = 301;
constexpr bool              kExampleDataEventPersistent1      = true;
constexpr bool              kExampleDataEventTstampExp1       = false;
constexpr uint32_t          kExampleDataEventType1            = 1;
constexpr const char*       kExampleDataEventUnit1            = "Celsius";
constexpr const char*       kExampleDataEventUnitExp1         = "none";

// Constants for the second ExampleDataEvent
constexpr uint16_t          kExampleDataEventId2           = 0x0016;
constexpr bool              kExampleDataEventEnable2       = true;
constexpr bool              kExampleDataEventTstampEnable2 = false;
constexpr astl_value_t      kExampleDataEventLatestValue2{.ui64 = 76ULL};
constexpr astl_value_type_t kExampleDataEventValueType2       = ASTL_VALUE_UINT64;
constexpr uint64_t          kExampleDataEventLatestTimestamp2 = 1678901234100ULL;
constexpr uint32_t          kExampleDataEventCompoInstanceId2 = 102;
constexpr uint32_t          kExampleDataEventCompoType2       = 202;
constexpr uint32_t          kExampleDataEventInstanceId2      = 302;
constexpr bool              kExampleDataEventPersistent2      = false;
constexpr bool              kExampleDataEventTstampExp2       = true;
constexpr uint32_t          kExampleDataEventType2            = 2;
constexpr const char*       kExampleDataEventUnit2            = "Percent";
constexpr const char*       kExampleDataEventUnitExp2         = "none";

inline std::vector<std::unique_ptr<DataEvent>> CreateTelemetryDataEvents() {
  std::vector<std::unique_ptr<DataEvent>> events;

  events.push_back(std::make_unique<ExampleDataEvent>(
      kExampleDataEventId1, kExampleDataEventEnable1, kExampleDataEventTstampEnable1, kExampleDataEventLatestValue1,
      kExampleDataEventValueType1, kExampleDataEventLatestTimestamp1, kExampleDataEventCompoInstanceId1,
      kExampleDataEventCompoType1, kExampleDataEventInstanceId1, kExampleDataEventPersistent1,
      kExampleDataEventTstampExp1, kExampleDataEventType1, kExampleDataEventUnit1, kExampleDataEventUnitExp1));

  events.push_back(std::make_unique<ExampleDataEvent>(
      kExampleDataEventId2, kExampleDataEventEnable2, kExampleDataEventTstampEnable2, kExampleDataEventLatestValue2,
      kExampleDataEventValueType2, kExampleDataEventLatestTimestamp2, kExampleDataEventCompoInstanceId2,
      kExampleDataEventCompoType2, kExampleDataEventInstanceId2, kExampleDataEventPersistent2,
      kExampleDataEventTstampExp2, kExampleDataEventType2, kExampleDataEventUnit2, kExampleDataEventUnitExp2));

  return events;
}

}  // namespace mock_sysfs

#endif  // INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP
