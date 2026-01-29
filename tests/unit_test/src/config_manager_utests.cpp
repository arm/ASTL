#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "config/astl_configuration.hpp"
#include "config/configuration_manager.hpp"
#include "config/metric_json_declaration.hpp"
#include "operation/scmi_operation_builder.hpp"
#include "operation/scmi_read_operation.hpp"

using trompeloeil::_;

inline const std::vector<std::string> kDataEventIds = {"0x1234"};

inline const astl::MetricConfig kTemperature{"SoC Temperature",           "SoC Temperature in Celsius",
                                             ASTL_UNITS_CELSIUS,          ASTL_VALUE_UINT64,
                                             ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE,
                                             astl::CollectorType::SCMI,   astl::ScmiOperationBuilder{0x1234}};

astl::ScmiDataEventId GetDataEventId(const astl::ResidencyMetricConfig::StateInfo& state_info) {
  if (const auto* scmi_builder = std::get_if<astl::ScmiOperationBuilder>(&state_info.operation_builder)) {
    return scmi_builder->GetDataEventId();
  }
  return astl::ScmiDataEventId{0xFFFFFFFF};  // invalid id
}

TEST_CASE("ConfigManager::StaticMetricConfig", "[ConfigManager]") {
  MockMetricManager mock_metric_manager;

  ALLOW_CALL(mock_metric_manager, RegisterMetric(_, _)).RETURN(ASTL_STATUS_NOT_IMPLEMENTED);
  // TODO(ASTL-101): Create unit tests for metric manager

  SECTION("Register a valid metric config") {
    // Create a new metric config directly (cannot copy since ExpressionFormula is move-only)
    auto metric_config = std::make_unique<astl::MetricConfig>(
        "SoC Temperature", "SoC Temperature in Celsius", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64,
        ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, astl::CollectorType::SCMI, astl::ScmiOperationBuilder{0x1234});

    REQUIRE(mock_metric_manager.RegisterMetric(std::move(metric_config), {}) == ASTL_STATUS_NOT_IMPLEMENTED);
  }

  SECTION("Register an invalid metric config") {
    auto invalid_metric_config = std::make_unique<astl::MetricConfig>(
        "SoC Temperature", "SoC Temperature for abc xyz", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64,
        ASTL_CATEGORY_UNCATEGORIZED, ASTL_METRIC_VALUE, astl::CollectorType::MMIO, astl::NullOperationBuilder{});

    REQUIRE(mock_metric_manager.RegisterMetric(std::move(invalid_metric_config), {}) == ASTL_STATUS_NOT_IMPLEMENTED);
  }
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("CreateMetricConfig for Residency Metric", "[ConfigManager]") {
  // Create a mock SCMI specification with the residency counter data event IDs
  astl::scmi::spec::ScmiSpecification mock_scmi_spec;
  mock_scmi_spec.layout = {
      // Layout for AP cores with C-state counters
      {.count        = 2,  // AP0 and AP1
       .start_offset = 0,
       .block_size   = 64,
       .members      = {{.base_de_id    = 0x1c71,
                         .name          = "C1_RESIDENCY_COUNTER",
                         .component     = "AP",
                         .description   = "C1 residency",
                         .unit          = "ticks",
                         .unit_exponent = 0,
                         .rel_offset    = 0x00},
                        {.base_de_id    = 0x1d82,
                         .name          = "C3_RESIDENCY_COUNTER",
                         .component     = "AP",
                         .description   = "C3 residency",
                         .unit          = "ticks",
                         .unit_exponent = 0,
                         .rel_offset    = 0x10},
                        {.base_de_id    = 0x1e93,
                         .name          = "C6_RESIDENCY_COUNTER",
                         .component     = "AP",
                         .description   = "C6 residency",
                         .unit          = "ticks",
                         .unit_exponent = 0,
                         .rel_offset    = 0x20}}}
  };

  std::vector<const astl::ITarget*> mock_scmi_targets;
  astl::Target                      mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
  mock_scmi_targets.push_back(&mock_target_tlm0);

  // Create a residency metric declaration
  astl::metrics::spec::MetricJsonDeclaration residency_declaration;
  residency_declaration.description         = "CPU C-State residency";
  residency_declaration.unit                = "seconds";
  residency_declaration.metric_type         = "residency";
  residency_declaration.collection.protocol = "scmi";
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
      astl::metrics::spec::CreateScmiMetricConfigs("C-State", residency_declaration, mock_scmi_spec, mock_scmi_targets);

  // Verify the configs were created successfully
  REQUIRE(metric_configs_result.has_value());
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE(metric_configs.size() == 2);

  // Verify it's a ResidencyMetricConfig (by attempting to cast)
  auto  configs_it           = metric_configs.begin();
  auto* residency_config_ap0 = dynamic_cast<astl::ResidencyMetricConfig*>(configs_it->first.get());
  configs_it++;
  auto* residency_config_ap1 = dynamic_cast<astl::ResidencyMetricConfig*>(configs_it->first.get());
  REQUIRE(residency_config_ap0 != nullptr);
  REQUIRE(residency_config_ap1 != nullptr);
  // since these metric_configs aren't in a specific order, let's make sure the first one corresponds to AP0
  if (residency_config_ap0->Name().ends_with("1")) {
    std::swap(residency_config_ap0, residency_config_ap1);
  }

  // Verify basic metric properties
  REQUIRE(residency_config_ap0->Name() == "C-State.0");
  REQUIRE(residency_config_ap0->Description() == "CPU C-State residency");
  REQUIRE(residency_config_ap0->Units() == ASTL_UNITS_SECONDS);
  REQUIRE(residency_config_ap0->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(residency_config_ap0->MetricType() == ASTL_METRIC_RESIDENCY);
  REQUIRE(residency_config_ap0->GetCollectorType() == astl::CollectorType::SCMI);

  // Verify the state info (data event IDs and tick frequencies) are correctly stored
  const auto& state_info = residency_config_ap0->GetStateInfo();
  // Verify AP0 state info (data event IDs and tick frequencies)
  REQUIRE(state_info.size() == 3);  // C1, C3, C6
  REQUIRE(state_info.at("C1").state_name == "C1");
  REQUIRE(GetDataEventId(state_info.at("C1")) == 0x00001c71);
  REQUIRE(state_info.at("C1").tick_frequency == 1000000.0);
  REQUIRE(state_info.at("C3").state_name == "C3");
  REQUIRE(GetDataEventId(state_info.at("C3")) == 0x00001d82);
  REQUIRE(state_info.at("C3").tick_frequency == 1000000.0);
  REQUIRE(state_info.at("C6").state_name == "C6");
  REQUIRE(GetDataEventId(state_info.at("C6")) == 0x00001e93);
  REQUIRE(state_info.at("C6").tick_frequency == 1000000.0);

  // now check the metric configuration for AP1
  REQUIRE(residency_config_ap1 != nullptr);

  // Verify basic metric properties
  REQUIRE(residency_config_ap1->Name() == "C-State.1");
  REQUIRE(residency_config_ap1->Description() == "CPU C-State residency");
  REQUIRE(residency_config_ap1->Units() == ASTL_UNITS_SECONDS);
  REQUIRE(residency_config_ap1->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(residency_config_ap1->MetricType() == ASTL_METRIC_RESIDENCY);
  REQUIRE(residency_config_ap1->GetCollectorType() == astl::CollectorType::SCMI);
}

TEST_CASE("CreateMetricConfig for Finite Set Metric", "[ConfigManager][FiniteSet]") {
  SECTION("Valid finite set metric configuration") {
    astl::scmi::spec::ScmiSpecification mock_scmi_spec;
    mock_scmi_spec.layout = {
        {.count        = 1,
         .start_offset = 0,
         .block_size   = 32,
         .members      = {{.base_de_id    = 0x1a69,
                           .name          = "P_STATE",
                           .component     = "AP",
                           .description   = "P-State",
                           .unit          = "",
                           .unit_exponent = 0,
                           .rel_offset    = 0x00}}}
    };

    std::vector<const astl::ITarget*> mock_scmi_targets;
    astl::Target mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
    mock_scmi_targets.push_back(&mock_target_tlm0);

    astl::metrics::spec::MetricJsonDeclaration finite_decl;
    finite_decl.description              = "Current CPU performance state (P-state)";
    finite_decl.unit                     = "";
    finite_decl.metric_type              = "finite_set";
    finite_decl.collection.protocol      = "scmi";
    finite_decl.collection.register_name = "P_STATE";

    // json finite_set_values representation: array of single-key objects
    std::vector<nlohmann::json> finite_json{nlohmann::json{{"P0", 0}}, nlohmann::json{{"P1", 1}},
                                            nlohmann::json{{"P2", 2}}, nlohmann::json{{"P3", 3}}};
    finite_decl.finite_set_values = finite_json;

    auto metric_configs_result =
        astl::metrics::spec::CreateScmiMetricConfigs("P-State", finite_decl, mock_scmi_spec, mock_scmi_targets);
    REQUIRE(metric_configs_result);
    auto metric_configs_on_targets = std::move(metric_configs_result.value());
    REQUIRE(metric_configs_on_targets.size() == 1);

    std::unordered_map<std::string, astl::FiniteSetMetricConfig*> by_name;
    for (auto& cfg_ptr : metric_configs_on_targets) {
      auto* fs_cfg = dynamic_cast<astl::FiniteSetMetricConfig*>(cfg_ptr.first.get());
      REQUIRE(fs_cfg != nullptr);
      by_name[fs_cfg->Name()] = fs_cfg;
    }
    REQUIRE(by_name.contains("AP.0.P_STATE"));  // fully qualified with component.index.name

    auto* ap0_cfg = by_name["AP.0.P_STATE"];
    REQUIRE(ap0_cfg->MetricType() == ASTL_METRIC_FINITE_SET_VALUE);

    // Expect 4 unique values
    REQUIRE(ap0_cfg->FiniteSetSize() == 4);

    std::vector<uint64_t> expected_set{0, 1, 2, 3};
    for (auto const& value : expected_set) {
      REQUIRE(ap0_cfg->GetFiniteSet().contains(astl::AstlValue{value}));
    }
  }
  // NOLINTEND(readability-function-cognitive-complexity)

  SECTION("Invalid finite set metric (empty values)") {
    astl::scmi::spec::ScmiSpecification mock_scmi_spec;
    mock_scmi_spec.layout = {
        {.count        = 1,
         .start_offset = 0,
         .block_size   = 32,
         .members      = {{.base_de_id    = 0x1a69,
                           .name          = "P_STATE",
                           .component     = "AP",
                           .description   = "P-State",
                           .unit          = "",
                           .unit_exponent = 0,
                           .rel_offset    = 0x00}}}
    };

    std::vector<const astl::ITarget*> mock_scmi_targets;
    astl::Target mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
    mock_scmi_targets.push_back(&mock_target_tlm0);

    astl::metrics::spec::MetricJsonDeclaration bad_decl;
    bad_decl.description              = "Bad P-State metric";
    bad_decl.unit                     = "";
    bad_decl.metric_type              = "finite_set";
    bad_decl.collection.protocol      = "scmi";
    bad_decl.collection.register_name = "P_STATE";
    // finite_set_values left empty (optional disengaged)

    auto bad_result =
        astl::metrics::spec::CreateScmiMetricConfigs("P-State", bad_decl, mock_scmi_spec, mock_scmi_targets);
    REQUIRE_FALSE(bad_result.has_value());
    REQUIRE(bad_result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

// Basic path / configuration retrieval tests
TEST_CASE("ConfigurationManager::GetAstlFilePath returns success", "[ConfigManager][Paths]") {
  auto result = astl::ConfigurationManager::GetAstlFilePath();
  if (!result) {
    std::cerr << "[DEBUG] GetAstlFilePath error code: " << astlStatusString(result.error()) << '\n';
  }
  REQUIRE(result);  // ensure success; do not inspect underlying path
}

TEST_CASE("ConfigurationManager::GetConfiguration returns configuration", "[ConfigManager][Paths]") {
  auto result = astl::ConfigurationManager::GetConfiguration();
  if (!result) {
    std::cerr << "[DEBUG] GetConfiguration error code: " << astlStatusString(result.error()) << '\n';
  }
  REQUIRE(result);  // ensure config parsed
  auto config = result.value();
  (void)config;  // suppress unused variable warning; no further inspection per requirements
}

// ===================== Category Parsing Tests =====================
TEST_CASE("ParseConfiguration missing category defaults to unknown/UNCATEGORIZED", "[ConfigManager][Category]") {
  // Test with metric declaration JSON that would be in a metrics file
  constexpr auto json_metrics_data = R"json(
  {
    "_comment": "Test metrics",
    "document": {
      "confidential": false
    },
    "metrics": {
      "CPU Power": {
        "description": "CPU power consumption",
        "metric_type": "value",
        "collection": {
          "protocol": "scmi",
          "register": "CPU_POWER"
        }
      }
    }
  }
  )json";

  // Parse the metrics JSON
  json json_data = json::parse(json_metrics_data);
  auto metrics   = json_data.at("metrics");

  astl::metrics::spec::MetricJsonDeclaration cpu_power_metric =
      metrics.at("CPU Power").get<astl::metrics::spec::MetricJsonDeclaration>();
  REQUIRE(cpu_power_metric.category == "unknown");  // default string

  // Build a metric config to verify enum mapping
  astl::scmi::spec::ScmiSpecification spec;
  spec.layout = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .members      = {{.base_de_id    = 0xabcd,
                         .name          = "CPU_POWER",
                         .component     = "AP",
                         .description   = "CPU Power",
                         .unit          = "W",
                         .unit_exponent = 0,
                         .rel_offset    = 0x00}}}
  };
  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);
  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("CPU Power", cpu_power_metric, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Category() == ASTL_CATEGORY_UNCATEGORIZED);
}

TEST_CASE("ParseConfiguration valid category string maps to enum", "[ConfigManager][Category]") {
  // Test with metric declaration JSON that would be in a metrics file
  constexpr auto json_metrics_data = R"json(
  {
    "_comment": "Test metrics",
    "document": {
      "confidential": false
    },
    "metrics": {
      "SoC Temperature": {
        "description": "Temperature in Celsius",
        "metric_type": "value",
        "category": "TEMPERATURE",
        "collection": {
          "protocol": "scmi",
          "register": "SOC_TEMP"
        }
      }
    }
  }
  )json";

  // Parse the metrics JSON
  json json_data = json::parse(json_metrics_data);
  auto metrics   = json_data.at("metrics");

  astl::metrics::spec::MetricJsonDeclaration soc_temp_metric =
      metrics.at("SoC Temperature").get<astl::metrics::spec::MetricJsonDeclaration>();
  REQUIRE(soc_temp_metric.category == "TEMPERATURE");

  // Build a metric config to verify enum mapping
  astl::scmi::spec::ScmiSpecification spec;
  spec.layout = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .members      = {{.base_de_id    = 0xdcba,
                         .name          = "SOC_TEMP",
                         .component     = "AP",
                         .description   = "SoC Temp",
                         .unit          = "C",
                         .unit_exponent = 0,
                         .rel_offset    = 0x00}}}
  };
  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);
  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("SoC Temperature", soc_temp_metric, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Category() == ASTL_CATEGORY_TEMPERATURE);
}

TEST_CASE("ParseConfiguration invalid category string maps to UNCATEGORIZED", "[ConfigManager][Category]") {
  // Test with metric declaration JSON that would be in a metrics file
  constexpr auto json_metrics_data = R"json(
  {
    "_comment": "Test metrics",
    "document": {
      "confidential": false
    },
    "metrics": {
      "GPU Power": {
        "description": "GPU power consumption",
        "metric_type": "value",
        "category": "THIS_IS_NOT_A_VALID_VALUE",
        "collection": {
          "protocol": "scmi",
          "register": "GPU_POWER"
        }
      }
    }
  }
  )json";

  // Parse the metrics JSON
  json json_data = json::parse(json_metrics_data);
  auto metrics   = json_data.at("metrics");

  astl::metrics::spec::MetricJsonDeclaration gpu_power_metric =
      metrics.at("GPU Power").get<astl::metrics::spec::MetricJsonDeclaration>();
  REQUIRE(gpu_power_metric.category == "THIS_IS_NOT_A_VALID_VALUE");

  // Build a metric config to verify enum mapping
  astl::scmi::spec::ScmiSpecification spec;
  spec.layout = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .members      = {{.base_de_id    = 0x1234,
                         .name          = "GPU_POWER",
                         .component     = "AP",
                         .description   = "GPU Power",
                         .unit          = "W",
                         .unit_exponent = 0,
                         .rel_offset    = 0x00}}}
  };
  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);
  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("GPU Power", gpu_power_metric, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Category() == ASTL_CATEGORY_UNCATEGORIZED);
}
