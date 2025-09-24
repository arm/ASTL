#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "common/scmi/scmi_read_operation.hpp"
#include "config/astl_configuration.hpp"
#include "config/configuration_manager.hpp"

using trompeloeil::_;

inline const std::vector<std::string> kDataEventIds = {"0x1234"};

inline const astl::MetricConfig kTemperature{"SoC Temperature",
                                             "SoC Temperature in Celsius",
                                             ASTL_UNITS_CELSIUS,
                                             ASTL_VALUE_UINT64,
                                             ASTL_METRIC_VALUE,
                                             astl::CollectorType::SCMI,
                                             astl::ScmiTargetToDataEventIdMap{{"AP0", {0x1234}}}};

TEST_CASE("ConfigManager::StaticMetricConfig", "[ConfigManager]") {
  MockMetricManager mock_metric_manager;

  ALLOW_CALL(mock_metric_manager, RegisterMetric(_, _)).RETURN(ASTL_STATUS_NOT_IMPLEMENTED);
  // TODO(ASTL-101): Create unit tests for metric manager

  SECTION("Register a valid metric config") {
    REQUIRE(mock_metric_manager.RegisterMetric(std::make_unique<astl::MetricConfig>(kTemperature), {}) ==
            ASTL_STATUS_NOT_IMPLEMENTED);
  }

  SECTION("Register an invalid metric config") {
    astl::ScmiTargetToDataEventIdMap invalid_data_event_ids{};

    auto invalid_metric_config = std::make_unique<astl::MetricConfig>(
        "SoC Temperature", "SoC Temperature for abc xyz", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64, ASTL_METRIC_VALUE,
        astl::CollectorType::MMIO, invalid_data_event_ids);

    REQUIRE(mock_metric_manager.RegisterMetric(std::move(invalid_metric_config), {}) == ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

TEST_CASE("ParseConfiguration", "[ConfigManager]") {
  constexpr auto     json_config_data = R"json(
  {
    "scmi_sysfs_telemetry_root_path": "~/tmp/fuse/scmi",

    "metrics": {
      "SoC Type": {
        "description": "Temperature in Celsius",
        "register": "SOC_TEMP",
        "unit": "C",
        "metric_type": "value",
        "collection_protocol": "scmi"
      },
      "Throttle Counts": {
        "description": "Number of thermal throttling events",
        "register": "THROTTLE_EVENTS",
        "unit": "",
        "metric_type": "delta",
        "collection_protocol": "scmi"
      }
    },

    "scmi_specification_path": "/etc/arm/astl/scmi_specification.json"
  }
  )json";
  std::istringstream json_data_stream{json_config_data};
  auto               result = astl::ParseConfiguration(json_data_stream);
  REQUIRE(result);
  auto config = result.value();
  REQUIRE(config.metric_declarations.size() == 2);
  REQUIRE(config.scmi_sysfs_telemetry_root_path == "~/tmp/fuse/scmi");
  REQUIRE(config.scmi_specification_path == "/etc/arm/astl/scmi_specification.json");
}

TEST_CASE("ParseConfiguration with Residency Metric", "[ConfigManager]") {
  constexpr auto json_config_data = R"json(
  {
    "scmi_sysfs_telemetry_root_path": "~/tmp/fuse/scmi",

    "metrics": {
      "C-State": {
        "description": "CPU C-State residency",
        "unit": "seconds",
        "metric_type": "residency",
        "collection_protocol": "scmi",
        "inferred_state": "Active",
        "states": {
          "C1": {
            "register": "C1_RESIDENCY_COUNTER",
            "tick_frequency": 1000000.0
          },
          "C3": {
            "register": "C3_RESIDENCY_COUNTER", 
            "tick_frequency": 1000000.0
          },
          "C6": {
            "register": "C6_RESIDENCY_COUNTER",
            "tick_frequency": 1000000.0
          }
        }
      }
    },

    "scmi_specification_path": "/etc/arm/astl/scmi_specification.json"
  }
  )json";

  std::istringstream json_data_stream{json_config_data};
  auto               result = astl::ParseConfiguration(json_data_stream);
  REQUIRE(result);

  auto config = result.value();
  REQUIRE(config.metric_declarations.size() == 1);
  REQUIRE(config.scmi_sysfs_telemetry_root_path == "~/tmp/fuse/scmi");
  REQUIRE(config.scmi_specification_path == "/etc/arm/astl/scmi_specification.json");

  // Verify residency metric specific fields
  auto& residency_metric = config.metric_declarations.at("C-State");
  REQUIRE(residency_metric.description == "CPU C-State residency");
  REQUIRE(residency_metric.unit == "seconds");
  REQUIRE(residency_metric.metric_type == "residency");
  REQUIRE(residency_metric.collection_protocol == "scmi");
  REQUIRE(residency_metric.register_name.empty());  // No top-level register for residency

  // Verify inferred state
  REQUIRE(residency_metric.inferred_state.has_value());
  REQUIRE(residency_metric.inferred_state.value() == "Active");

  // Verify states configuration
  REQUIRE(residency_metric.states.has_value());
  auto& states = residency_metric.states.value();
  REQUIRE(states.size() == 3);

  // Verify C1 state
  REQUIRE(states.contains("C1"));
  REQUIRE(states.at("C1").contains("register"));
  REQUIRE(states.at("C1").at("register").get<std::string>() == "C1_RESIDENCY_COUNTER");
  REQUIRE(states.at("C1").contains("tick_frequency"));
  REQUIRE(states.at("C1").at("tick_frequency").get<double>() == 1000000.0);

  // Verify C3 state
  REQUIRE(states.contains("C3"));
  REQUIRE(states.at("C3").at("register").get<std::string>() == "C3_RESIDENCY_COUNTER");
  REQUIRE(states.at("C3").at("tick_frequency").get<double>() == 1000000.0);

  // Verify C6 state
  REQUIRE(states.contains("C6"));
  REQUIRE(states.at("C6").at("register").get<std::string>() == "C6_RESIDENCY_COUNTER");
  REQUIRE(states.at("C6").at("tick_frequency").get<double>() == 1000000.0);
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("CreateMetricConfig for Residency Metric", "[ConfigManager]") {
  // Create a mock SCMI layout with the residency counter data event IDs
  astl::scmi::Layout mock_layout;
  mock_layout.members["AP0"] = {
      {"C1_RESIDENCY_COUNTER", {.name = "AP0_C1_RESIDENCY_COUNTER", .de_id = 0x00001c71}},
      {"C3_RESIDENCY_COUNTER", {.name = "AP0_C3_RESIDENCY_COUNTER", .de_id = 0x00001d82}},
      {"C6_RESIDENCY_COUNTER", {.name = "AP0_C6_RESIDENCY_COUNTER", .de_id = 0x00001e93}}
  };
  mock_layout.members["AP1"] = {
      {"C1_RESIDENCY_COUNTER", {.name = "AP1_C1_RESIDENCY_COUNTER", .de_id = 0x00011c71}},
      {"C3_RESIDENCY_COUNTER", {.name = "AP1_C3_RESIDENCY_COUNTER", .de_id = 0x00011d82}},
      {"C6_RESIDENCY_COUNTER", {.name = "AP1_C6_RESIDENCY_COUNTER", .de_id = 0x00011e93}}
  };
  astl::scmi::ScmiSpecification mock_scmi_spec;
  mock_scmi_spec.layout = std::move(mock_layout);

  std::vector<const astl::ITarget*> mock_scmi_targets;
  astl::Target                      mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
  mock_scmi_targets.push_back(&mock_target_tlm0);

  // Create a residency metric declaration
  astl::MetricJsonDeclaration residency_declaration;
  residency_declaration.description         = "CPU C-State residency";
  residency_declaration.unit                = "seconds";
  residency_declaration.metric_type         = "residency";
  residency_declaration.collection_protocol = "scmi";
  residency_declaration.inferred_state      = "Active";

  // Set up states configuration
  nlohmann::json states_json;
  states_json["C1"] = {
      {"register",       "C1_RESIDENCY_COUNTER"},
      {"tick_frequency", 1000000.0             }
  };
  states_json["C3"] = {
      {"register",       "C3_RESIDENCY_COUNTER"},
      {"tick_frequency", 1000000.0             }
  };
  states_json["C6"] = {
      {"register",       "C6_RESIDENCY_COUNTER"},
      {"tick_frequency", 1000000.0             }
  };

  std::map<std::string, nlohmann::json> states_map;
  for (const auto& [key, value] : states_json.items()) {
    states_map[key] = value;
  }
  residency_declaration.states = states_map;

  // Create the metric config
  auto metric_configs_result =
      astl::CreateMetricConfigs("C-State", residency_declaration, mock_scmi_spec, mock_scmi_targets);

  // Verify the configs were created successfully
  REQUIRE(metric_configs_result.has_value());
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE(metric_configs.size() == 2);

  // Verify it's a ResidencyMetricConfig (by attempting to cast)
  auto* residency_config_ap0 = dynamic_cast<astl::ResidencyMetricConfig*>(metric_configs[0].get());
  auto* residency_config_ap1 = dynamic_cast<astl::ResidencyMetricConfig*>(metric_configs[1].get());
  REQUIRE(residency_config_ap0 != nullptr);
  REQUIRE(residency_config_ap1 != nullptr);
  // since these metric_configs aren't in a specific order, let's make sure the first one corresponds to AP0
  if (residency_config_ap0->Name().starts_with("AP1")) {
    std::swap(residency_config_ap0, residency_config_ap1);
  }

  // Verify basic metric properties
  REQUIRE(residency_config_ap0->Name() == "AP0_C-State");
  REQUIRE(residency_config_ap0->Description() == "CPU C-State residency");
  REQUIRE(residency_config_ap0->Units() == ASTL_UNITS_SECONDS);
  REQUIRE(residency_config_ap0->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(residency_config_ap0->MetricType() == ASTL_METRIC_RESIDENCY);
  REQUIRE(residency_config_ap0->GetCollectorType() == astl::CollectorType::SCMI);

  // Verify the state info (data event IDs and tick frequencies) are correctly stored
  const auto& state_info = residency_config_ap0->GetStateInfo();
  REQUIRE(state_info.size() == 1);  // AP0 and AP1

  // Verify AP0 state info (data event IDs and tick frequencies)
  REQUIRE(state_info.contains(mock_target_tlm0.Name()));
  const auto& tlm_0_ap0_state_info = state_info.at(mock_target_tlm0.Name());
  REQUIRE(tlm_0_ap0_state_info.size() == 3);  // C1, C3, C6
  REQUIRE(tlm_0_ap0_state_info.at("C1").state_name == "C1");
  REQUIRE(tlm_0_ap0_state_info.at("C1").data_event_id == 0x00001c71);
  REQUIRE(tlm_0_ap0_state_info.at("C1").tick_frequency == 1000000.0);
  REQUIRE(tlm_0_ap0_state_info.at("C3").state_name == "C3");
  REQUIRE(tlm_0_ap0_state_info.at("C3").data_event_id == 0x00001d82);
  REQUIRE(tlm_0_ap0_state_info.at("C3").tick_frequency == 1000000.0);
  REQUIRE(tlm_0_ap0_state_info.at("C6").state_name == "C6");
  REQUIRE(tlm_0_ap0_state_info.at("C6").data_event_id == 0x00001e93);
  REQUIRE(tlm_0_ap0_state_info.at("C6").tick_frequency == 1000000.0);

  // now check the metric configuration for AP1
  REQUIRE(residency_config_ap1 != nullptr);

  // Verify basic metric properties
  REQUIRE(residency_config_ap1->Name() == "AP1_C-State");
  REQUIRE(residency_config_ap1->Description() == "CPU C-State residency");
  REQUIRE(residency_config_ap1->Units() == ASTL_UNITS_SECONDS);
  REQUIRE(residency_config_ap1->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(residency_config_ap1->MetricType() == ASTL_METRIC_RESIDENCY);
  REQUIRE(residency_config_ap1->GetCollectorType() == astl::CollectorType::SCMI);

  // Verify the state info (data event IDs and tick frequencies) are correctly stored
  const auto& state_info_ap1 = residency_config_ap1->GetStateInfo();
  REQUIRE(state_info_ap1.size() == 1);  // AP0 and AP1

  // Verify AP0 state info (data event IDs and tick frequencies)
  REQUIRE(state_info_ap1.contains(mock_target_tlm0.Name()));
  const auto& tlm0_state_info_ap1 = state_info_ap1.at(mock_target_tlm0.Name());
  REQUIRE(tlm0_state_info_ap1.size() == 3);  // C1, C3, C6
  REQUIRE(tlm0_state_info_ap1.at("C1").state_name == "C1");
  REQUIRE(tlm0_state_info_ap1.at("C1").data_event_id == 0x00011c71);
  REQUIRE(tlm0_state_info_ap1.at("C1").tick_frequency == 1000000.0);
  REQUIRE(tlm0_state_info_ap1.at("C3").state_name == "C3");
  REQUIRE(tlm0_state_info_ap1.at("C3").data_event_id == 0x00011d82);
  REQUIRE(tlm0_state_info_ap1.at("C3").tick_frequency == 1000000.0);
  REQUIRE(tlm0_state_info_ap1.at("C6").state_name == "C6");
  REQUIRE(tlm0_state_info_ap1.at("C6").data_event_id == 0x00011e93);
  REQUIRE(tlm0_state_info_ap1.at("C6").tick_frequency == 1000000.0);
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("Invalid file path", "[ConfigManager]") {
  ASTL_INIT_STRUCT(astl_initialization_parameters_t, init_params, ._configuration_file_path = "not_a_valid_file.wav");
  auto config_results = astl::ConfigurationManager::GetConfiguration(&init_params);
  REQUIRE(config_results.error() == ASTL_STATUS_BAD_CONFIGURATION);
}
