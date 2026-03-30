// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "common/capabilities.hpp"
#include "config/astl_configuration.hpp"
#include "libsensors/libsensors_metric_builder.hpp"
#include "libsensors/libsensors_target.hpp"
#include "metric/metric_manager.hpp"
#include "mock_libsensors.hpp"

using astl::Capabilities;
using astl::CollectorCapability;
using astl::CollectorType;
using astl::MetricManager;
using astl::SystemCapability;
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

auto MakeCaps() -> Capabilities {
  return Capabilities{{CollectorCapability{CollectorType::LIBSENSORS}}, {SystemCapability{}}};
}

auto GetRegisteredMetricNames(const MetricManager& manager, const astl::ITarget* target) -> std::vector<std::string> {
  auto handles_or_error = manager.GetAvailableMetrics(target);
  REQUIRE(handles_or_error.has_value());

  std::vector<std::string> names;
  for (const auto* const handle : handles_or_error.value()) {
    astl_metric_props_t properties{};
    properties.size = sizeof(astl_metric_props_t);
    REQUIRE(manager.GetProperties(handle, &properties) == ASTL_STATUS_SUCCESS);
    REQUIRE(properties.name != nullptr);
    names.emplace_back(properties.name);
  }
  return names;
}

auto GetRegisteredMetricProperties(const MetricManager& manager, const astl::ITarget* target)
    -> std::vector<astl_metric_props_t> {
  auto handles_or_error = manager.GetAvailableMetrics(target);
  REQUIRE(handles_or_error.has_value());

  std::vector<astl_metric_props_t> properties;
  for (const auto* const handle : handles_or_error.value()) {
    astl_metric_props_t metric_properties{};
    metric_properties.size = sizeof(astl_metric_props_t);
    REQUIRE(manager.GetProperties(handle, &metric_properties) == ASTL_STATUS_SUCCESS);
    properties.push_back(metric_properties);
  }
  return properties;
}

}  // namespace

TEST_CASE("RegisterLibsensorsMetrics disambiguates duplicate labels across chips", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1.0).RETURN(0);

  std::string       chip1_prefix = "chip";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 1};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  std::string       chip2_prefix = "chip";
  sensors_bus_id    chip2_bus    = {.type = 1, .nr = 2};
  std::string       chip2_path   = "/test/chip2";
  sensors_chip_name chip2 = {.prefix = chip2_prefix.data(), .bus = chip2_bus, .addr = 0x2, .path = chip2_path.data()};

  static auto            feature1_name   = kMakeMutableCString("power1");
  static auto            feature2_name   = kMakeMutableCString("power2");
  static auto            cpu_power_label = kMakeMutableCString("CPU power");
  static sensors_feature feature1        = {
             .name = feature1_name.data(), .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature feature2 = {
      .name = feature2_name.data(), .number = 2, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature1 = {.name    = feature1_name.data(),
                                           .number  = 11,
                                           .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  static sensors_subfeature subfeature2 = {.name    = feature2_name.data(),
                                           .number  = 22,
                                           .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  trompeloeil::sequence     sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip1);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "chip-1-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature1)).IN_SEQUENCE(sequence).RETURN(cpu_power_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature1, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip2);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "chip-1-2"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature2)).IN_SEQUENCE(sequence).RETURN(cpu_power_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature2, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps()};
  auto          libsensors_target_1 =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-1-1", "test target 1", "chip-1-1", harness.api);
  auto libsensors_target_2 =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-1-2", "test target 2", "chip-1-2", harness.api);
  const auto*                                                          target_ptr_1 = libsensors_target_1.get();
  const auto*                                                          target_ptr_2 = libsensors_target_2.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr_1, target_ptr_2}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names_1 = GetRegisteredMetricNames(metric_manager, target_ptr_1);
  const auto metric_names_2 = GetRegisteredMetricNames(metric_manager, target_ptr_2);
  REQUIRE(metric_names_1.size() == 1);
  REQUIRE(metric_names_2.size() == 1);
  REQUIRE(metric_names_1[0] == "chip-1-1_CPU_power");
  REQUIRE(metric_names_2[0] == "chip-1-2_CPU_power");
}

TEST_CASE("RegisterLibsensorsMetrics resolves duplicate labels on the same chip", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 2, .nr = 7};
  std::string       chip_path   = "/test/chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static auto            feature1_name    = kMakeMutableCString("temp1");
  static auto            feature2_name    = kMakeMutableCString("temp2");
  static auto            board_temp_label = kMakeMutableCString("Board Temp");
  static sensors_feature feature1         = {
              .name = feature1_name.data(), .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature feature2 = {
      .name = feature2_name.data(), .number = 2, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature1 = {.name    = feature1_name.data(),
                                           .number  = 101,
                                           .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  static sensors_subfeature subfeature2 = {.name    = feature2_name.data(),
                                           .number  = 102,
                                           .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  trompeloeil::sequence     sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "chip-2-7"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature1)).IN_SEQUENCE(sequence).RETURN(board_temp_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature1, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature2)).IN_SEQUENCE(sequence).RETURN(board_temp_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature2, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-2-7", "test target", "chip-2-7", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names = GetRegisteredMetricNames(metric_manager, target_ptr);
  REQUIRE(metric_names.size() == 2);
  REQUIRE(metric_names[0] != metric_names[1]);
  REQUIRE(std::ranges::find(metric_names, "chip-2-7_Board_Temp_1") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-2-7_Board_Temp_2") != metric_names.end());
}

TEST_CASE("RegisterLibsensorsMetrics registers fan speed metrics", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1200.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 3, .nr = 5};
  std::string       chip_path   = "/test/fan-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static auto            feature_name  = kMakeMutableCString("fan1");
  static auto            cpu_fan_label = kMakeMutableCString("CPU Fan");
  static sensors_feature feature       = {
            .name = feature_name.data(), .number = 1, .type = SENSORS_FEATURE_FAN, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature = {.name    = feature_name.data(),
                                          .number  = 201,
                                          .type    = SENSORS_SUBFEATURE_FAN_INPUT,
                                          .mapping = 0,
                                          .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "chip-3-5"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature)).IN_SEQUENCE(sequence).RETURN(cpu_fan_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_FAN_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-3-5", "fan target", "chip-3-5", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(properties.size() == 1);
  REQUIRE(std::string(properties.front().name) == "chip-3-5_CPU_Fan");
  REQUIRE(std::string(properties.front().description) == "Fan speed reading for CPU_Fan on chip chip-3-5");
  REQUIRE(properties.front().units == ASTL_UNITS_RPM);
}

TEST_CASE("RegisterLibsensorsMetrics keeps sensors with zero values", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 4, .nr = 2};
  std::string       chip_path   = "/test/zero-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static auto            feature_name    = kMakeMutableCString("temp1");
  static auto            zero_temp_label = kMakeMutableCString("Zero Temp");
  static sensors_feature feature         = {
              .name = feature_name.data(), .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature = {.name    = feature_name.data(),
                                          .number  = 301,
                                          .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                          .mapping = 0,
                                          .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "chip-4-2"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature)).IN_SEQUENCE(sequence).RETURN(zero_temp_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 301, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 0.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-4-2", "zero target", "chip-4-2", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names = GetRegisteredMetricNames(metric_manager, target_ptr);
  REQUIRE(metric_names.size() == 1);
  REQUIRE(metric_names[0] == "chip-4-2_Zero_Temp");
}

TEST_CASE("RegisterLibsensorsMetrics skips sensors with unavailable values", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 5, .nr = 9};
  std::string       chip_path   = "/test/unavailable-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static auto            feature_name            = kMakeMutableCString("power1");
  static auto            unavailable_power_label = kMakeMutableCString("Unavailable Power");
  static sensors_feature feature                 = {
                      .name = feature_name.data(), .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature = {.name    = feature_name.data(),
                                          .number  = 401,
                                          .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                          .mapping = 0,
                                          .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(CopyChipName(_1, _2, "chip-5-9"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(unavailable_power_label.data());
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 401, _)).IN_SEQUENCE(sequence).RETURN(-1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-5-9", "unavailable target", "chip-5-9", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names = GetRegisteredMetricNames(metric_manager, target_ptr);
  REQUIRE(metric_names.empty());
}
