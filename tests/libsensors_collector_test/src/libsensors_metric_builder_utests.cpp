// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
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

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

auto MakeCaps() -> Capabilities {
  return Capabilities{{CollectorCapability{CollectorType::LIBSENSORS}}, {SystemCapability{}}};
}

auto MakeMetricGroupDescriptions() -> MetricManager::MetricGroupDescriptionMap {
  return {
      {"auxiliary",  "Unit test auxiliary metrics" },
      {"core",       "Unit test core metrics"      },
      {"cpu",        "Unit test CPU metrics"       },
      {"gpu",        "Unit test GPU metrics"       },
      {"nic",        "Unit test NIC metrics"       },
      {"power",      "Unit test power metrics"     },
      {"thermal",    "Unit test thermal metrics"   },
      {"throttling", "Unit test throttling metrics"},
  };
}

auto GetTestMetricsDir() -> std::filesystem::path {
  return std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path() / "config" / "metrics";
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

auto FindMetricPropertyByName(const std::vector<astl_metric_props_t>& properties, std::string_view metric_name)
    -> const astl_metric_props_t& {
  const auto iter = std::ranges::find_if(properties, [metric_name](const auto& property) {
    return property.name != nullptr && metric_name == property.name;
  });
  REQUIRE(iter != properties.end());
  return *iter;
}

struct RecordingProcessedSampleSink : astl::IProcessedSampleSink {
  std::vector<astl::ProcessedSampledData> samples;

  auto SinkProcessedSamples(const astl::ITarget* /*target*/, const astl::IMetric* /*metric*/,
                            std::span<const astl::ProcessedSampledData> processed_samples)
      -> astl_status_code override {
    samples.insert(samples.end(), processed_samples.begin(), processed_samples.end());
    return ASTL_STATUS_SUCCESS;
  }
};

}  // namespace

TEST_CASE("RegisterLibsensorsMetrics disambiguates duplicate labels across chips", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1.0).RETURN(0);

  std::string       chip1_prefix = "chip";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 1};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  std::string       chip2_prefix = "chip";
  sensors_bus_id    chip2_bus    = {.type = 1, .nr = 2};
  std::string       chip2_path   = "/test/chip2";
  sensors_chip_name chip2 = {.prefix = chip2_prefix.data(), .bus = chip2_bus, .addr = 0x2, .path = chip2_path.data()};

  static char            feature1_name[] = "power1";
  static char            feature2_name[] = "power2";
  static sensors_feature feature1        = {
             .name = feature1_name, .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature feature2 = {
      .name = feature2_name, .number = 2, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature1 = {.name    = feature1_name,
                                           .number  = 11,
                                           .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  static sensors_subfeature subfeature2 = {.name    = feature2_name,
                                           .number  = 22,
                                           .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  trompeloeil::sequence     sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip1);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-1-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature1))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("CPU power"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature1, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip2);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-1-2"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature2))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("CPU power"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature2, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
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

TEST_CASE("RegisterLibsensorsMetrics uses stable family instance prefixes for address-bearing chips",
          "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 42.0).RETURN(0);

  const auto    temp_root = std::filesystem::temp_directory_path() / "astl_libsensors_metric_stable_names";
  TempFileGuard temp_root_guard{temp_root};
  WriteTextFile(temp_root / "libsensors" / "libsensors_nvme-pci.json", R"json(
{
  "document": {
    "confidential": false
  },
  "metrics": {
    "Composite": {
      "description": "Composite NVMe temperature",
      "unit": "celsius",
      "metric_type": "value",
      "identifier": "TEMPERATURE",
      "metric_groups": ["thermal"],
      "collection": {
        "register": "Composite",
        "protocol": "libsensors"
      }
    }
  }
}
)json");

  std::string       chip1_prefix = "nvme";
  sensors_bus_id    chip1_bus    = {.type = 1, .nr = 1};
  std::string       chip1_path   = "/test/chip1";
  sensors_chip_name chip1 = {.prefix = chip1_prefix.data(), .bus = chip1_bus, .addr = 0x1, .path = chip1_path.data()};

  std::string       chip2_prefix = "nvme";
  sensors_bus_id    chip2_bus    = {.type = 1, .nr = 2};
  std::string       chip2_path   = "/test/chip2";
  sensors_chip_name chip2 = {.prefix = chip2_prefix.data(), .bus = chip2_bus, .addr = 0x2, .path = chip2_path.data()};

  static char            feature1_name[] = "temp1";
  static char            feature2_name[] = "temp2";
  static sensors_feature feature1        = {
             .name = feature1_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature feature2 = {
      .name = feature2_name, .number = 2, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature1 = {.name    = feature1_name,
                                           .number  = 11,
                                           .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  static sensors_subfeature subfeature2 = {.name    = feature2_name,
                                           .number  = 22,
                                           .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip1);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "nvme-pci-40100"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature1))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Composite"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature1, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip2);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "nvme-pci-20100"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature2))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Composite"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature2, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = temp_root;

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto libsensors_target_1 = std::make_unique<astl::LibsensorsTarget>("libsensors_nvme-pci-40100", "nvme target 1",
                                                                      "nvme-pci-40100", harness.api);
  auto libsensors_target_2 = std::make_unique<astl::LibsensorsTarget>("libsensors_nvme-pci-20100", "nvme target 2",
                                                                      "nvme-pci-20100", harness.api);
  const auto*                                                          target_ptr_1 = libsensors_target_1.get();
  const auto*                                                          target_ptr_2 = libsensors_target_2.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr_1, target_ptr_2}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names_1 = GetRegisteredMetricNames(metric_manager, target_ptr_1);
  const auto metric_names_2 = GetRegisteredMetricNames(metric_manager, target_ptr_2);
  REQUIRE(metric_names_1 == std::vector<std::string>{"nvme-pci-1_Composite"});
  REQUIRE(metric_names_2 == std::vector<std::string>{"nvme-pci-2_Composite"});
}

TEST_CASE("RegisterLibsensorsMetrics resolves duplicate labels on the same chip", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 2, .nr = 7};
  std::string       chip_path   = "/test/chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature1_name[] = "temp1";
  static char            feature2_name[] = "temp2";
  static sensors_feature feature1        = {
             .name = feature1_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature feature2 = {
      .name = feature2_name, .number = 2, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature1 = {.name    = feature1_name,
                                           .number  = 101,
                                           .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  static sensors_subfeature subfeature2 = {.name    = feature2_name,
                                           .number  = 102,
                                           .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                           .mapping = 0,
                                           .flags   = SENSORS_MODE_R};
  trompeloeil::sequence     sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-2-7"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature1))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Board Temp"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature1, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature2))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Board Temp"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature2, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature2);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
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
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1200.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 3, .nr = 5};
  std::string       chip_path   = "/test/fan-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature_name[] = "fan1";
  static sensors_feature feature        = {
             .name = feature_name, .number = 1, .type = SENSORS_FEATURE_FAN, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature = {
      .name = feature_name, .number = 201, .type = SENSORS_SUBFEATURE_FAN_INPUT, .mapping = 0, .flags = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-3-5"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("CPU Fan"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_FAN_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
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
  REQUIRE(std::string(properties.front().description) == "Fan speed reading for CPU Fan");
  REQUIRE(properties.front().units == ASTL_UNITS_RPM);
  REQUIRE(properties.front().identifier == ASTL_METRIC_IDENTIFIER_FAN_SPEED);
}

TEST_CASE("RegisterLibsensorsMetrics keeps sensors with zero values", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 4, .nr = 2};
  std::string       chip_path   = "/test/zero-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature_name[] = "temp1";
  static sensors_feature feature        = {
             .name = feature_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature = {.name    = feature_name,
                                          .number  = 301,
                                          .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                          .mapping = 0,
                                          .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-4-2"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Zero Temp"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 301, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 0.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-4-2", "zero target", "chip-4-2", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names = GetRegisteredMetricNames(metric_manager, target_ptr);
  REQUIRE(metric_names.size() == 1);
  REQUIRE(metric_names.front() == "chip-4-2_Zero_Temp");
}

TEST_CASE("RegisterLibsensorsMetrics supports additional libsensors feature families", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 10, .nr = 2};
  std::string       chip_path   = "/test/multi-feature-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char in_feature_name[]          = "in1";
  static char curr_feature_name[]        = "curr1";
  static char energy_feature_name[]      = "energy1";
  static char humidity_feature_name[]    = "humidity1";
  static char vid_feature_name[]         = "vid1";
  static char intrusion_feature_name[]   = "intrusion0";
  static char beep_enable_feature_name[] = "beep_enable";

  static sensors_feature in_feature = {
      .name = in_feature_name, .number = 1, .type = SENSORS_FEATURE_IN, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature curr_feature = {
      .name = curr_feature_name, .number = 2, .type = SENSORS_FEATURE_CURR, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature energy_feature = {
      .name = energy_feature_name, .number = 3, .type = SENSORS_FEATURE_ENERGY, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature humidity_feature = {.name             = humidity_feature_name,
                                             .number           = 4,
                                             .type             = SENSORS_FEATURE_HUMIDITY,
                                             .first_subfeature = 0,
                                             .padding1         = 0};
  static sensors_feature vid_feature      = {
           .name = vid_feature_name, .number = 5, .type = SENSORS_FEATURE_VID, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature intrusion_feature   = {.name             = intrusion_feature_name,
                                                .number           = 6,
                                                .type             = SENSORS_FEATURE_INTRUSION,
                                                .first_subfeature = 0,
                                                .padding1         = 0};
  static sensors_feature beep_enable_feature = {.name             = beep_enable_feature_name,
                                                .number           = 7,
                                                .type             = SENSORS_FEATURE_BEEP_ENABLE,
                                                .first_subfeature = 0,
                                                .padding1         = 0};

  static sensors_subfeature in_subfeature       = {.name    = in_feature_name,
                                                   .number  = 1001,
                                                   .type    = SENSORS_SUBFEATURE_IN_INPUT,
                                                   .mapping = 0,
                                                   .flags   = SENSORS_MODE_R};
  static sensors_subfeature curr_subfeature     = {.name    = curr_feature_name,
                                                   .number  = 1002,
                                                   .type    = SENSORS_SUBFEATURE_CURR_INPUT,
                                                   .mapping = 0,
                                                   .flags   = SENSORS_MODE_R};
  static sensors_subfeature energy_subfeature   = {.name    = energy_feature_name,
                                                   .number  = 1003,
                                                   .type    = SENSORS_SUBFEATURE_ENERGY_INPUT,
                                                   .mapping = 0,
                                                   .flags   = SENSORS_MODE_R};
  static sensors_subfeature humidity_subfeature = {.name    = humidity_feature_name,
                                                   .number  = 1004,
                                                   .type    = SENSORS_SUBFEATURE_HUMIDITY_INPUT,
                                                   .mapping = 0,
                                                   .flags   = SENSORS_MODE_R};
  static sensors_subfeature vid_subfeature      = {
           .name = vid_feature_name, .number = 1005, .type = SENSORS_SUBFEATURE_VID, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature intrusion_subfeature   = {.name    = intrusion_feature_name,
                                                      .number  = 1006,
                                                      .type    = SENSORS_SUBFEATURE_INTRUSION_ALARM,
                                                      .mapping = 0,
                                                      .flags   = SENSORS_MODE_R};
  static sensors_subfeature beep_enable_subfeature = {.name    = beep_enable_feature_name,
                                                      .number  = 1007,
                                                      .type    = SENSORS_SUBFEATURE_BEEP_ENABLE,
                                                      .mapping = 0,
                                                      .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-10-2"))
      .RETURN(0);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&in_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &in_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Vcore"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &in_feature, SENSORS_SUBFEATURE_IN_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&in_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&curr_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &curr_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Icore"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &curr_feature, SENSORS_SUBFEATURE_CURR_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&curr_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&energy_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &energy_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Package Energy"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &energy_feature, SENSORS_SUBFEATURE_ENERGY_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&energy_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&humidity_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &humidity_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Ambient Humidity"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &humidity_feature, SENSORS_SUBFEATURE_HUMIDITY_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&humidity_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&vid_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &vid_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("VID"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &vid_feature, SENSORS_SUBFEATURE_VID))
      .IN_SEQUENCE(sequence)
      .RETURN(&vid_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&intrusion_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &intrusion_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Chassis Intrusion"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &intrusion_feature, SENSORS_SUBFEATURE_INTRUSION_ALARM))
      .IN_SEQUENCE(sequence)
      .RETURN(&intrusion_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&beep_enable_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &beep_enable_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Alarm Beep"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &beep_enable_feature, SENSORS_SUBFEATURE_BEEP_ENABLE))
      .IN_SEQUENCE(sequence)
      .RETURN(&beep_enable_subfeature);

  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto libsensors_target = std::make_unique<astl::LibsensorsTarget>("libsensors_chip-10-2", "multi feature target",
                                                                    "chip-10-2", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(properties.size() == 7);

  const auto& vcore_property = FindMetricPropertyByName(properties, "chip-10-2_Vcore");
  REQUIRE(vcore_property.units == ASTL_UNITS_VOLTS);
  REQUIRE(vcore_property.identifier == ASTL_METRIC_IDENTIFIER_VOLTAGE);

  const auto& icore_property = FindMetricPropertyByName(properties, "chip-10-2_Icore");
  REQUIRE(icore_property.units == ASTL_UNITS_AMPS);
  REQUIRE(icore_property.identifier == ASTL_METRIC_IDENTIFIER_CURRENT);

  const auto& energy_property = FindMetricPropertyByName(properties, "chip-10-2_Package_Energy");
  REQUIRE(energy_property.units == ASTL_UNITS_JOULES);
  REQUIRE(energy_property.identifier == ASTL_METRIC_IDENTIFIER_ENERGY);

  const auto& humidity_property = FindMetricPropertyByName(properties, "chip-10-2_Ambient_Humidity");
  REQUIRE(humidity_property.units == ASTL_UNITS_PERCENT);
  REQUIRE(humidity_property.identifier == ASTL_METRIC_IDENTIFIER_HUMIDITY);

  const auto& vid_property = FindMetricPropertyByName(properties, "chip-10-2_VID");
  REQUIRE(vid_property.units == ASTL_UNITS_VOLTS);
  REQUIRE(vid_property.identifier == ASTL_METRIC_IDENTIFIER_VOLTAGE);

  const auto& intrusion_property = FindMetricPropertyByName(properties, "chip-10-2_Chassis_Intrusion");
  REQUIRE(intrusion_property.units == ASTL_UNITS_NONE);
  REQUIRE(intrusion_property.identifier == ASTL_METRIC_IDENTIFIER_STATUS);

  const auto& beep_property = FindMetricPropertyByName(properties, "chip-10-2_Alarm_Beep");
  REQUIRE(beep_property.units == ASTL_UNITS_NONE);
  REQUIRE(beep_property.identifier == ASTL_METRIC_IDENTIFIER_STATUS);
}

TEST_CASE("RegisterLibsensorsMetrics skips sensors with unavailable values", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 5, .nr = 9};
  std::string       chip_path   = "/test/unavailable-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature_name[] = "power1";
  static sensors_feature feature        = {
             .name = feature_name, .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature subfeature = {.name    = feature_name,
                                          .number  = 401,
                                          .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                          .mapping = 0,
                                          .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-5-9"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Unavailable Power"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 401, _)).IN_SEQUENCE(sequence).RETURN(-1);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
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

TEST_CASE("RegisterLibsensorsMetrics uses per-target JSON declarations when available", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 10.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 6, .nr = 1};
  std::string       chip_path   = "/test/configured-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            power_feature_name[] = "power1";
  static char            temp_feature_name[]  = "temp1";
  static sensors_feature power_feature        = {
             .name = power_feature_name, .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature temp_feature = {
      .name = temp_feature_name, .number = 2, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature power_subfeature = {.name    = power_feature_name,
                                                .number  = 501,
                                                .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};
  static sensors_subfeature temp_subfeature  = {.name    = temp_feature_name,
                                                .number  = 502,
                                                .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-6-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&power_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &power_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("CPU power"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &power_feature, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&power_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 501, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 10.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&temp_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &temp_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Board Temp"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &temp_feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&temp_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 502, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 42.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = GetTestMetricsDir();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-6-1", "configured target", "chip-6-1", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(properties.size() == 1);
  REQUIRE(std::string(properties.front().name) == "chip-6-1_CPU_power");
  REQUIRE(std::string(properties.front().description) == "Configured power reading for CPU power");
  REQUIRE(properties.front().units == ASTL_UNITS_WATTS);
  REQUIRE(properties.front().identifier == ASTL_METRIC_IDENTIFIER_POWER);
}

TEST_CASE("RegisterLibsensorsMetrics applies family-level JSON declarations without hiding discovered sensors",
          "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 10.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 11, .nr = 7};
  std::string       chip_path   = "/test/family-configured-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            power_feature_name[] = "power1";
  static char            temp_feature_name[]  = "temp1";
  static sensors_feature power_feature        = {
             .name = power_feature_name, .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_feature temp_feature = {
      .name = temp_feature_name, .number = 2, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature power_subfeature = {.name    = power_feature_name,
                                                .number  = 1101,
                                                .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};
  static sensors_subfeature temp_subfeature  = {.name    = temp_feature_name,
                                                .number  = 1102,
                                                .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-11-7"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&power_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &power_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("CPU power"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &power_feature, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&power_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 1101, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_3 = 14000.0)
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&temp_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &temp_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Board Temp"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &temp_feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&temp_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 1102, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 42.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  const auto config_root = std::filesystem::temp_directory_path() /
                           ("astl_libsensors_family_config_fixture_" +
                            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  std::error_code remove_err;
  std::filesystem::remove_all(config_root, remove_err);
  REQUIRE_FALSE(remove_err);
  WriteTextFile(config_root / "libsensors" / "libsensors_chip-11.json", R"({
  "_comment": "Metrics definitions for the chip-11 libsensors family",
  "document": {
    "confidential": false
  },
  "metrics": {
    "CPU_power": {
      "description": "Configured family power reading for CPU power",
      "unit": "watts",
      "metric_type": "value",
      "identifier": "POWER",
      "metric_groups": ["power", "cpu"],
      "collection": {
        "register": "CPU power",
        "protocol": "libsensors"
      },
      "formula": "value / 1000"
    }
  }
}
)");
  configuration.metrics_dir_path = config_root;

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto libsensors_target = std::make_unique<astl::LibsensorsTarget>("libsensors_chip-11-7", "family configured target",
                                                                    "chip-11-7", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(properties.size() == 2);

  const auto& power_property = FindMetricPropertyByName(properties, "chip-11-1_CPU_power");
  REQUIRE(std::string(power_property.description) == "Configured family power reading for CPU power");
  REQUIRE(power_property.units == ASTL_UNITS_WATTS);
  REQUIRE(power_property.identifier == ASTL_METRIC_IDENTIFIER_POWER);

  const auto& temp_property = FindMetricPropertyByName(properties, "chip-11-1_Board_Temp");
  REQUIRE(std::string(temp_property.description) == "Temperature reading for Board Temp");
  REQUIRE(temp_property.units == ASTL_UNITS_CELSIUS);
  REQUIRE(temp_property.identifier == ASTL_METRIC_IDENTIFIER_TEMPERATURE);
}

TEST_CASE("RegisterLibsensorsMetrics matches declared register names when labels differ",
          "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 55.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 8, .nr = 1};
  std::string       chip_path   = "/test/register-name-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            temp_feature_name[] = "temp1";
  static sensors_feature temp_feature        = {
             .name = temp_feature_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature temp_subfeature = {.name    = temp_feature_name,
                                               .number  = 801,
                                               .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                               .mapping = 0,
                                               .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-8-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&temp_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &temp_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Wifi adapter temp"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &temp_feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&temp_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 801, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 55.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = GetTestMetricsDir();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-8-1", "register target", "chip-8-1", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(properties.size() == 1);
  REQUIRE(std::string(properties.front().name) == "chip-8-1_Wifi_adapter_temp");
  REQUIRE(std::string(properties.front().description) == "Configured temperature reading for the wifi adapter");
  REQUIRE(properties.front().units == ASTL_UNITS_CELSIUS);
  REQUIRE(properties.front().identifier == ASTL_METRIC_IDENTIFIER_TEMPERATURE);
}

TEST_CASE("RegisterLibsensorsMetrics applies configured formula scaling", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 6, .nr = 1};
  std::string       chip_path   = "/test/configured-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            power_feature_name[] = "power1";
  static sensors_feature power_feature        = {
             .name = power_feature_name, .number = 1, .type = SENSORS_FEATURE_POWER, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature power_subfeature = {.name    = power_feature_name,
                                                .number  = 501,
                                                .type    = SENSORS_SUBFEATURE_POWER_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-6-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&power_feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &power_feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("CPU power"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &power_feature, SENSORS_SUBFEATURE_POWER_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&power_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 501, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 10.0).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = GetTestMetricsDir();

  MetricManager                metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  RecordingProcessedSampleSink sink;
  REQUIRE(metric_manager.RegisterProcessedSampleSink(&sink) == ASTL_STATUS_SUCCESS);

  auto libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-6-1", "configured target", "chip-6-1", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  auto handles_or_error = metric_manager.GetAvailableMetrics(target_ptr);
  REQUIRE(handles_or_error.has_value());
  REQUIRE(handles_or_error->size() == 1);

  auto operations_or_error = metric_manager.GetRequiredOperations(*handles_or_error, target_ptr);
  REQUIRE(operations_or_error.has_value());
  REQUIRE(operations_or_error->operationsOnSample.size() == 1);

  const auto                operation_id = operations_or_error->operationsOnSample.front()->GetId();
  astl::ClockCorrelationMap correlations;
  correlations[operation_id] = astl::OperationClockCorrelation{astl::ProcessedSampleTimestamp{}, uint64_t{0},
                                                               astl::MakeTickRatio<astl::SampleMicroseconds>()};
  metric_manager.SetClockCorrelations(correlations);
  astl::RawSamplesMap raw_samples;
  raw_samples[target_ptr] = {
      astl::RawSampledData{operation_id, astl::AstlValue{14000.0}}
  };

  REQUIRE(metric_manager.ProcessRawSamples(raw_samples) == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.samples.size() == 1);
  auto scaled_value = sink.samples.front().value.ToDouble();
  REQUIRE(scaled_value.has_value());
  REQUIRE(*scaled_value == Catch::Approx(14.0));
}

TEST_CASE("RegisterLibsensorsMetrics creates composite thermal limit metrics", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 7, .nr = 1};
  std::string       chip_path   = "/test/nvme-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature_name[] = "temp1";
  static sensors_feature feature        = {
             .name = feature_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature input_subfeature = {.name    = feature_name,
                                                .number  = 601,
                                                .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};
  static sensors_subfeature min_subfeature   = {
        .name = feature_name, .number = 602, .type = SENSORS_SUBFEATURE_TEMP_MIN, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature max_subfeature = {
      .name = feature_name, .number = 603, .type = SENSORS_SUBFEATURE_TEMP_MAX, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature crit_subfeature = {
      .name = feature_name, .number = 604, .type = SENSORS_SUBFEATURE_TEMP_CRIT, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature emergency_subfeature = {.name    = feature_name,
                                                    .number  = 605,
                                                    .type    = SENSORS_SUBFEATURE_TEMP_EMERGENCY,
                                                    .mapping = 0,
                                                    .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-7-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Composite"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&input_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 601, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 34.85).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_MIN))
      .IN_SEQUENCE(sequence)
      .RETURN(&min_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 602, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(*_3 = -20.15)
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_MAX))
      .IN_SEQUENCE(sequence)
      .RETURN(&max_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 603, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 83.85).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_CRIT))
      .IN_SEQUENCE(sequence)
      .RETURN(&crit_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 604, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 88.85).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_EMERGENCY))
      .IN_SEQUENCE(sequence)
      .RETURN(&emergency_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 605, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 93.85).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = GetTestMetricsDir();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-7-1", "nvme target", "chip-7-1", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names = GetRegisteredMetricNames(metric_manager, target_ptr);
  REQUIRE(metric_names.size() == 5);
  REQUIRE(std::ranges::find(metric_names, "chip-7-1_Composite") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-7-1_Composite_thermal_limit_low") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-7-1_Composite_thermal_limit_high") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-7-1_Composite_thermal_limit_critical") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-7-1_Composite_thermal_limit_emergency") != metric_names.end());

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-7-1_Composite_thermal_limit_low" &&
           std::string(property.description) == "Configured low thermal limit for Composite";
  }));
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-7-1_Composite_thermal_limit_high" &&
           std::string(property.description) == "Configured high thermal limit for Composite";
  }));
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-7-1_Composite_thermal_limit_critical" &&
           std::string(property.description) == "Configured critical thermal limit for Composite";
  }));
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-7-1_Composite_thermal_limit_emergency" &&
           std::string(property.description) == "Configured emergency thermal limit for Composite";
  }));
}

TEST_CASE("RegisterLibsensorsMetrics supports inherited libsensors config templates", "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 9, .nr = 1};
  std::string       chip_path   = "/test/inherited-config-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature_name[] = "temp1";
  static sensors_feature feature        = {
             .name = feature_name, .number = 1, .type = SENSORS_FEATURE_TEMP, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature input_subfeature = {.name    = feature_name,
                                                .number  = 901,
                                                .type    = SENSORS_SUBFEATURE_TEMP_INPUT,
                                                .mapping = 0,
                                                .flags   = SENSORS_MODE_R};
  static sensors_subfeature max_subfeature   = {
        .name = feature_name, .number = 902, .type = SENSORS_SUBFEATURE_TEMP_MAX, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature emergency_subfeature = {.name    = feature_name,
                                                    .number  = 903,
                                                    .type    = SENSORS_SUBFEATURE_TEMP_EMERGENCY,
                                                    .mapping = 0,
                                                    .flags   = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-9-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Composite"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&input_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 901, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 44.5).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_MIN))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_MAX))
      .IN_SEQUENCE(sequence)
      .RETURN(&max_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 902, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 81.5).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_CRIT))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_TEMP_EMERGENCY))
      .IN_SEQUENCE(sequence)
      .RETURN(&emergency_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_value(_, 903, _)).IN_SEQUENCE(sequence).SIDE_EFFECT(*_3 = 91.5).RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = GetTestMetricsDir();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-9-1", "inherited target", "chip-9-1", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto metric_names = GetRegisteredMetricNames(metric_manager, target_ptr);
  REQUIRE(metric_names.size() == 3);
  REQUIRE(std::ranges::find(metric_names, "chip-9-1_Composite") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-9-1_Composite_thermal_limit_high") != metric_names.end());
  REQUIRE(std::ranges::find(metric_names, "chip-9-1_Composite_thermal_limit_emergency") != metric_names.end());

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-9-1_Composite" &&
           std::string(property.description) == "Temperature reading for Composite";
  }));
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-9-1_Composite_thermal_limit_high" &&
           std::string(property.description) == "Configured high thermal limit for Composite";
  }));
  REQUIRE(std::ranges::any_of(properties, [](const auto& property) {
    return std::string(property.name) == "chip-9-1_Composite_thermal_limit_emergency" &&
           std::string(property.description) == "Configured emergency thermal limit for Composite";
  }));
}

TEST_CASE("RegisterLibsensorsMetrics expands configured voltage derived metrics with overrides",
          "[libsensors_metric_builder]") {
  MockSensorsApiTestHarness harness;
  auto&                     mock_libsensors = harness.mock_libsensors;
  ALLOW_CALL(*mock_libsensors, sensors_get_subfeature(_, _, _)).RETURN(nullptr);
  ALLOW_CALL(*mock_libsensors, sensors_get_value(_, _, _)).SIDE_EFFECT(*_3 = 1.0).RETURN(0);

  std::string       chip_prefix = "chip";
  sensors_bus_id    chip_bus    = {.type = 10, .nr = 1};
  std::string       chip_path   = "/test/voltage-derived-chip";
  sensors_chip_name chip = {.prefix = chip_prefix.data(), .bus = chip_bus, .addr = 0x1, .path = chip_path.data()};

  static char            feature_name[] = "in1";
  static sensors_feature feature        = {
             .name = feature_name, .number = 1, .type = SENSORS_FEATURE_IN, .first_subfeature = 0, .padding1 = 0};
  static sensors_subfeature input_subfeature = {
      .name = feature_name, .number = 1101, .type = SENSORS_SUBFEATURE_IN_INPUT, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature min_subfeature = {
      .name = feature_name, .number = 1102, .type = SENSORS_SUBFEATURE_IN_MIN, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature crit_subfeature = {
      .name = feature_name, .number = 1103, .type = SENSORS_SUBFEATURE_IN_CRIT, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature alarm_subfeature = {
      .name = feature_name, .number = 1104, .type = SENSORS_SUBFEATURE_IN_ALARM, .mapping = 0, .flags = SENSORS_MODE_R};
  static sensors_subfeature beep_subfeature = {
      .name = feature_name, .number = 1105, .type = SENSORS_SUBFEATURE_IN_BEEP, .mapping = 0, .flags = SENSORS_MODE_R};

  trompeloeil::sequence sequence;
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(&chip);
  REQUIRE_CALL(*mock_libsensors, sensors_snprintf_chip_name(_, _, _))
      .IN_SEQUENCE(sequence)
      .SIDE_EFFECT(std::snprintf(_1, _2, "chip-10-1"))
      .RETURN(0);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(&feature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_label(_, &feature))
      .IN_SEQUENCE(sequence)
      .RETURN(const_cast<char*>("Vcore"));
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_INPUT))
      .IN_SEQUENCE(sequence)
      .RETURN(&input_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_MIN))
      .IN_SEQUENCE(sequence)
      .RETURN(&min_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_MAX))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_LCRIT))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_CRIT))
      .IN_SEQUENCE(sequence)
      .RETURN(&crit_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_AVERAGE))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_LOWEST))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_HIGHEST))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_ALARM))
      .IN_SEQUENCE(sequence)
      .RETURN(&alarm_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_MIN_ALARM))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_MAX_ALARM))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_BEEP))
      .IN_SEQUENCE(sequence)
      .RETURN(&beep_subfeature);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_LCRIT_ALARM))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_subfeature(_, &feature, SENSORS_SUBFEATURE_IN_CRIT_ALARM))
      .IN_SEQUENCE(sequence)
      .RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_features(_, _)).IN_SEQUENCE(sequence).RETURN(nullptr);
  REQUIRE_CALL(*mock_libsensors, sensors_get_detected_chips(nullptr, _)).IN_SEQUENCE(sequence).RETURN(nullptr);

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration             = configuration_result.value();
  configuration.metrics_dir_path = GetTestMetricsDir();

  MetricManager metric_manager{MakeCaps(), MakeMetricGroupDescriptions()};
  auto          libsensors_target =
      std::make_unique<astl::LibsensorsTarget>("libsensors_chip-10-1", "voltage target", "chip-10-1", harness.api);
  const auto*                                                          target_ptr = libsensors_target.get();
  std::unordered_map<CollectorType, std::vector<const astl::ITarget*>> targets_by_collector{
      {CollectorType::LIBSENSORS, {target_ptr}}
  };

  REQUIRE(astl::RegisterLibsensorsMetrics(configuration, targets_by_collector, &metric_manager) == ASTL_STATUS_SUCCESS);

  const auto properties = GetRegisteredMetricProperties(metric_manager, target_ptr);
  REQUIRE(properties.size() == 5);

  const auto& base_property = FindMetricPropertyByName(properties, "chip-10-1_Vcore");
  REQUIRE(std::string(base_property.description) == "Configured voltage reading for Vcore");
  REQUIRE(base_property.units == ASTL_UNITS_VOLTS);
  REQUIRE(base_property.identifier == ASTL_METRIC_IDENTIFIER_VOLTAGE);

  const auto& min_property = FindMetricPropertyByName(properties, "chip-10-1_Vcore_min");
  REQUIRE(std::string(min_property.description) == "Configured minimum voltage for Vcore");
  REQUIRE(min_property.units == ASTL_UNITS_VOLTS);
  REQUIRE(min_property.identifier == ASTL_METRIC_IDENTIFIER_VOLTAGE);

  const auto& crit_property = FindMetricPropertyByName(properties, "chip-10-1_Vcore_crit");
  REQUIRE(std::string(crit_property.description) == "Configured critical voltage for Vcore");
  REQUIRE(crit_property.units == ASTL_UNITS_VOLTS);
  REQUIRE(crit_property.identifier == ASTL_METRIC_IDENTIFIER_VOLTAGE);

  const auto& alarm_property = FindMetricPropertyByName(properties, "chip-10-1_Vcore_alarm");
  REQUIRE(std::string(alarm_property.description) == "Configured voltage alarm for Vcore");
  REQUIRE(alarm_property.units == ASTL_UNITS_NONE);
  REQUIRE(alarm_property.identifier == ASTL_METRIC_IDENTIFIER_STATUS);

  const auto& beep_property = FindMetricPropertyByName(properties, "chip-10-1_Vcore_beep");
  REQUIRE(std::string(beep_property.description) == "Configured voltage beep status for Vcore");
  REQUIRE(beep_property.units == ASTL_UNITS_NONE);
  REQUIRE(beep_property.identifier == ASTL_METRIC_IDENTIFIER_STATUS);
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
