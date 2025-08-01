#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "config/astl_configuration.hpp"
#include "config/configuration_manager.hpp"
#include "config/static_metric_config.hpp"

using trompeloeil::_;

TEST_CASE("ConfigManager::StaticMetricConfig", "[ConfigManager]") {
  MockMetricManager mock_metric_manager;

  ALLOW_CALL(mock_metric_manager, RegisterMetric(_)).RETURN(ASTL_STATUS_NOT_IMPLEMENTED);
  // TODO(ASTL-101): Create unit tests for metric manager

  SECTION("Register a valid metric config") {
    REQUIRE(mock_metric_manager.RegisterMetric(std::make_unique<astl::MetricConfig>(astl::kTemperature)) ==
            ASTL_STATUS_NOT_IMPLEMENTED);
  }

  SECTION("Register an invalid metric config") {
    std::vector<std::string> invalid_data_event_ids{};

    auto invalid_metric_config = std::make_unique<astl::MetricConfig>(
        "SoC Temperature", "SoC Temperature for abc xyz", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64, ASTL_METRIC_VALUE,
        astl::CollectorType::MMIO, invalid_data_event_ids);

    REQUIRE(mock_metric_manager.RegisterMetric(std::move(invalid_metric_config)) == ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

TEST_CASE("ParseConfiguration", "[ConfigManager]") {
  constexpr auto     json_config_data = R"json(
  {
    "scmi_sysfs_telemetry_root_path": "~/tmp/fuse/scmi",

    "metrics": [
      "cpu_cycles",
      "SoC Temperature"
    ],

    "scmi_specification_path": "/etc/arm/astl/scmi_specification.json"
  }
  )json";
  std::istringstream json_data_stream{json_config_data};
  auto               result = astl::ParseConfiguration(json_data_stream);
  REQUIRE(result);
  auto config = result.value();
  REQUIRE(config.metric_names_to_use.size() == 2);
  REQUIRE(config.scmi_sysfs_telemetry_root_path == "~/tmp/fuse/scmi");
  REQUIRE(config.scmi_specification_path == "/etc/arm/astl/scmi_specification.json");
}

TEST_CASE("Invalid file path", "[ConfigManager]") {
  ASTL_INIT_STRUCT(astl_initialization_parameters_t, init_params, ._configuration_file_path = "not_a_valid_file.wav");
  auto config_results = astl::ConfigurationManager::GetConfiguration(&init_params);
  REQUIRE(config_results.error() == ASTL_STATUS_BAD_CONFIGURATION);
}
