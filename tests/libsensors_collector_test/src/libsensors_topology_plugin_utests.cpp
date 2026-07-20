// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "astl/astl.h"
#include "libsensors/libsensors_topology_plugin.hpp"
#include "mock_libsensors.hpp"  // for global mock_libsensors object

using namespace std::chrono_literals;

using trompeloeil::_;

namespace {

struct MutableCStringFactory {
  template <typename Array>
  constexpr auto operator()(const Array& text) const {
    return std::to_array(text);
  }
};

constexpr auto kMakeMutableCString = MutableCStringFactory{};

auto CopyChipName(char* buffer, size_t buffer_size, std::string_view name) -> void {
  REQUIRE(buffer != nullptr);
  REQUIRE(buffer_size > 0U);
  std::fill_n(buffer, buffer_size, '\0');
  if (buffer_size == 1U) {
    return;
  }
  const auto copy_size = std::min(name.size(), buffer_size - 1U);
  std::copy_n(name.data(), copy_size, buffer);
}

auto WriteTextFile(const std::filesystem::path& path, std::string_view contents) -> void {
  std::error_code err;
  std::filesystem::create_directories(path.parent_path(), err);
  REQUIRE_FALSE(err);

  std::ofstream output(path, std::ios::out | std::ios::trunc);
  REQUIRE(output.is_open());
  output << contents;
  REQUIRE(output.good());
}

}  // namespace

TEST_CASE("LibsensorsTopologyPlugin::ScanForTargets", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;

  // set up expectations and test harness for libsensors calls
  ALLOW_CALL(*mock_libsensors, sensors_init(_)).RETURN(0);
  ALLOW_CALL(*mock_libsensors, sensors_cleanup());

  // chip1 is a mock chip with several features
  std::string       chip1_prefix = "snsr";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 2};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  // allow calls to sensors_snprintf_chip_name to succeed
  ALLOW_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .SIDE_EFFECT(CopyChipName(_1, _2, "snsr-1-2"))
      .RETURN(0);

  // we have one chip with several features
  // in sequence, expect calls to sensors_get_features that return a temperature sensor, fan sensor, and voltage sensor
  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)  // first call, index=0
      .RETURN(&chip1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)  // first call, index=0
      .RETURN([]() -> sensors_feature* {
        static auto            temp1_name   = kMakeMutableCString("temp1");
        static sensors_feature feature_temp = {
            .name = temp1_name.data(), .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
        return &feature_temp;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 1)  // second call, index=1
      .RETURN([]() -> sensors_feature* {
        static auto            fan1_name   = kMakeMutableCString("fan1");
        static sensors_feature feature_fan = {
            .name = fan1_name.data(), .number = 2, .type = SENSORS_FEATURE_FAN, .first_subfeature = 0, .padding1 = 0};
        return &feature_fan;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 2)  // third call, index=2, for voltage
      .RETURN([]() -> sensors_feature* {
        static auto            in1_name   = kMakeMutableCString("in1");
        static sensors_feature feature_in = {
            .name = in1_name.data(), .number = 3, .type = SENSORS_FEATURE_IN, .first_subfeature = 0, .padding1 = 0};
        return &feature_in;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 3)  // fourth call, index=3, for power
      .RETURN([]() -> sensors_feature* {
        static auto            power1_name   = kMakeMutableCString("power1");
        static sensors_feature feature_power = {.name             = power1_name.data(),
                                                .number           = 4,
                                                .type             = SENSORS_FEATURE_POWER,
                                                .first_subfeature = 0,
                                                .padding1         = 0};
        return &feature_power;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 4)  // fifth call, index=4, for humidity
      .RETURN([]() -> sensors_feature* {
        static auto            humidity1_name   = kMakeMutableCString("humidity1");
        static sensors_feature feature_humidity = {.name             = humidity1_name.data(),
                                                   .number           = 5,
                                                   .type             = SENSORS_FEATURE_HUMIDITY,
                                                   .first_subfeature = 0,
                                                   .padding1         = 0};
        return &feature_humidity;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 5)  // sixth call, index=5, for VID
      .RETURN([]() -> sensors_feature* {
        static auto            vid1_name   = kMakeMutableCString("vid1");
        static sensors_feature feature_vid = {
            .name = vid1_name.data(), .number = 6, .type = SENSORS_FEATURE_VID, .first_subfeature = 0, .padding1 = 0};
        return &feature_vid;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 6)  // seventh call, index=6
      .RETURN(nullptr);      // end of features for this chip
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 1)  // second call, index=1
      .RETURN(nullptr);      // end of chips

  // generate the stimulus: scan for targets, enumerating chips and features
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto& configuration = configuration_result.value();
  auto  result = astl::LibsensorsTopologyPlugin::detail::ScanForTargetsWithLibsensors(configuration, harness.api);

  // make assertions on the results
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  REQUIRE(result->at(0)->Name() == "libsensors_snsr-1-2");
}

TEST_CASE("LibsensorsTopologyPlugin::ScanForTargets creates one target per chip", "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;

  ALLOW_CALL(*mock_libsensors, sensors_init(_)).RETURN(0);
  ALLOW_CALL(*mock_libsensors, sensors_cleanup());

  std::string       chip1_prefix = "snsr";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 2};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  std::string       chip2_prefix = "snsr";
  sensors_bus_id    chip2_bus    = {.type = 1, .nr = 3};
  std::string       chip2_path   = "/test/chip2";
  sensors_chip_name chip2 = {.prefix = chip2_prefix.data(), .bus = chip2_bus, .addr = 0x2, .path = chip2_path.data()};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)
      .RETURN(&chip1);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "snsr-1-2"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)
      .RETURN([]() -> sensors_feature* {
        static auto            temp1_name   = kMakeMutableCString("temp1");
        static sensors_feature feature_temp = {
            .name = temp1_name.data(), .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
        return &feature_temp;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_2 = 1).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 1)
      .RETURN(&chip2);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "snsr-1-3"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)
      .RETURN([]() -> sensors_feature* {
        static auto            power1_name   = kMakeMutableCString("power1");
        static sensors_feature feature_power = {.name             = power1_name.data(),
                                                .number           = 1,
                                                .type             = SENSORS_FEATURE_POWER,
                                                .first_subfeature = 0,
                                                .padding1         = 0};
        return &feature_power;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_2 = 1).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 2)
      .RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto& configuration = configuration_result.value();
  auto  result = astl::LibsensorsTopologyPlugin::detail::ScanForTargetsWithLibsensors(configuration, harness.api);

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 2);
  REQUIRE(result->at(0)->Name() == "libsensors_snsr-1-2");
  REQUIRE(result->at(1)->Name() == "libsensors_snsr-1-3");
}

TEST_CASE("LibsensorsTopologyPlugin::ScanForTargets keeps raw chip names even when family metadata exists",
          "[libsensors_collector]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;

  ALLOW_CALL(*mock_libsensors, sensors_init(_)).RETURN(0);
  ALLOW_CALL(*mock_libsensors, sensors_cleanup());

  const auto temp_root = std::filesystem::temp_directory_path() /
                         ("astl_libsensors_topology_stable_names_" +
                          std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  TempFileGuard temp_root_guard{temp_root};
  WriteTextFile(temp_root / "libsensors" / "libsensors_nvme-pci.json",
                R"json({"document":{"confidential":false},"metrics":{}})json");

  std::string       chip1_prefix = "nvme";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 1};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  std::string       chip2_prefix = "nvme";
  sensors_bus_id    chip2_bus    = {.type = 1, .nr = 2};
  std::string       chip2_path   = "/test/chip2";
  sensors_chip_name chip2 = {.prefix = chip2_prefix.data(), .bus = chip2_bus, .addr = 0x2, .path = chip2_path.data()};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)
      .RETURN(&chip1);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "nvme-pci-40100"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)
      .RETURN([]() -> sensors_feature* {
        static auto            temp1_name   = kMakeMutableCString("temp1");
        static sensors_feature feature_temp = {
            .name = temp1_name.data(), .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
        return &feature_temp;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_2 = 1).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 1)
      .RETURN(&chip2);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "nvme-pci-20100"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 0)
      .RETURN([]() -> sensors_feature* {
        static auto            temp1_name   = kMakeMutableCString("temp1");
        static sensors_feature feature_temp = {
            .name = temp1_name.data(), .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
        return &feature_temp;
      }());
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_2 = 1).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_2 = 2)
      .RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = temp_root;

  auto result = astl::LibsensorsTopologyPlugin::detail::ScanForTargetsWithLibsensors(configuration, harness.api);

  REQUIRE(result.has_value());
  REQUIRE(result->size() == 2);
  REQUIRE(result->at(0)->Name() == "libsensors_nvme-pci-40100");
  REQUIRE(result->at(1)->Name() == "libsensors_nvme-pci-20100");
}
