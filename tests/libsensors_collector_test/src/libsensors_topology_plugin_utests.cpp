#include <algorithm>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl.h"
#include "common/libsensors.hpp"
#include "mock_libsensors.hpp"  // for global mock_libsensors object
#include "topology/libsensors_topology_plugin.hpp"

using namespace std::chrono_literals;

using trompeloeil::_;

TEST_CASE("LibsensorsTopologyPlugin::ScanForTargets", "[libsensors_collector]") {
  // set up expectations and test harness for libsensors calls
  ALLOW_CALL(mock_libsensors, sensors_init(_)).RETURN(0);
  ALLOW_CALL(mock_libsensors, sensors_cleanup());

  // chip1 is a mock chip with several features
  std::string       chip1_prefix = "snsr";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 2};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  // allow calls to sensors_snprintf_chip_name to succeed
  ALLOW_CALL(mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .SIDE_EFFECT(std::snprintf(_1, _2, "%s-%d-%d", chip1.prefix, chip1.bus.type, chip1.bus.nr))
      .RETURN(0);

  // we have one chip with several features
  // in sequence, expect calls to sensors_get_features that return a temperature sensor, fan sensor, and voltage sensor
  trompeloeil::sequence sequence;
  REQUIRE_CALL(mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)  // first call, index=0
      .RETURN(&chip1);
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)  // first call, index=0
      .RETURN([]() -> sensors_feature* {
        static char*           temp1_name   = const_cast<char*>("temp1");
        static sensors_feature feature_temp = {
            .name = temp1_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
        return &feature_temp;
      }());
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 1)  // second call, index=1
      .RETURN([]() -> sensors_feature* {
        static char*           fan1_name   = const_cast<char*>("fan1");
        static sensors_feature feature_fan = {
            .name = fan1_name, .number = 2, .type = SENSORS_FEATURE_FAN, .first_subfeature = 0, .padding1 = 0};
        return &feature_fan;
      }());
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 2)  // third call, index=2, for voltage
      .RETURN([]() -> sensors_feature* {
        static char*           in1_name   = const_cast<char*>("in1");
        static sensors_feature feature_in = {
            .name = in1_name, .number = 3, .type = SENSORS_FEATURE_IN, .first_subfeature = 0, .padding1 = 0};
        return &feature_in;
      }());
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 3)  // fourth call, index=3, for power
      .RETURN([]() -> sensors_feature* {
        static char*           power1_name   = const_cast<char*>("power1");
        static sensors_feature feature_power = {
            .name = power1_name, .number = 4, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
        return &feature_power;
      }());
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 4)  // fifth call, index=4, for humidity
      .RETURN([]() -> sensors_feature* {
        static char*           humidity1_name   = const_cast<char*>("humidity1");
        static sensors_feature feature_humidity = {.name             = humidity1_name,
                                                   .number           = 5,
                                                   .type             = SENSORS_FEATURE_HUMIDITY,
                                                   .first_subfeature = 0,
                                                   .padding1         = 0};
        return &feature_humidity;
      }());
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 5)  // sixth call, index=5, for VID
      .RETURN([]() -> sensors_feature* {
        static char*           vid1_name   = const_cast<char*>("vid1");
        static sensors_feature feature_vid = {
            .name = vid1_name, .number = 6, .type = SENSORS_FEATURE_VID, .first_subfeature = 0, .padding1 = 0};
        return &feature_vid;
      }());
  REQUIRE_CALL(mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 6)  // seventh call, index=6
      .RETURN(nullptr);      // end of features for this chip
  REQUIRE_CALL(mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 1)  // second call, index=1
      .RETURN(nullptr);      // end of chips

  // generate the stimulus: scan for targets, enumerating chips and features
  astl::AstlConfiguration configuration;
  auto                    result = astl::LibsensorsTopologyPlugin::ScanForTargets(configuration);

  // make assertions on the results
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
}
