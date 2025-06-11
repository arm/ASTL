#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/trompeloeil.hpp>

#include "../../mock_classes.hpp"
#include "astl/astl_errors.h"
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
