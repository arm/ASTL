// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cmath>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "astl_internal_status.hpp"
#include "common/capabilities.hpp"
#include "common/metric_config.hpp"
#include "config/astl_configuration.hpp"
#include "config/configuration_manager.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "operation/scmi_operation_builder.hpp"
#include "operation/scmi_read_operation.hpp"

using trompeloeil::_;

inline const std::vector<std::string> kDataEventIds = {"0x1234"};

inline const astl::MetricConfig kTemperature{
    "SoC Temperature",         "SoC Temperature in Celsius",      ASTL_UNITS_CELSIUS,
    ASTL_VALUE_UINT64,         ASTL_METRIC_IDENTIFIER_UNKNOWN,    ASTL_METRIC_VALUE,
    astl::CollectorType::SCMI, astl::ScmiOperationBuilder{0x1234}};

astl::ScmiDataEventId GetDataEventId(const astl::ResidencyMetricConfig::StateInfo& state_info) {
  if (const auto* scmi_builder = std::get_if<astl::ScmiOperationBuilder>(&state_info.operation_builder)) {
    return scmi_builder->GetDataEventId();
  }
  return astl::ScmiDataEventId{0xFFFFFFFF};  // invalid id
}

void SetScmiCollection(astl::metrics::spec::MetricJsonDeclaration& metric_declaration,
                       std::optional<std::string>                  register_name    = std::nullopt,
                       std::optional<std::string>                  component_filter = std::nullopt,
                       std::optional<std::string>                  instance_filter  = std::nullopt) {
  metric_declaration.collection.protocol = "scmi";
  metric_declaration.collection.register_name.clear();
  metric_declaration.collection.scmi_component_filter.reset();
  metric_declaration.collection.scmi_instance_filter.reset();
  metric_declaration.collection.raw_json = nlohmann::json{
      {"protocol", "scmi"}
  };
  if (register_name.has_value()) {
    metric_declaration.collection.register_name        = *register_name;
    metric_declaration.collection.raw_json["register"] = *register_name;
  }
  if (component_filter.has_value()) {
    metric_declaration.collection.scmi_component_filter             = *component_filter;
    metric_declaration.collection.raw_json["scmi_component_filter"] = *component_filter;
  }
  if (instance_filter.has_value()) {
    metric_declaration.collection.scmi_instance_filter             = *instance_filter;
    metric_declaration.collection.raw_json["scmi_instance_filter"] = *instance_filter;
  }
}

auto MakeSingleRegisterScmiSpec(std::string register_name, std::string component = "AP", std::string unit = "")
    -> astl::scmi::spec::ScmiSpecification {
  astl::scmi::spec::DataEvent         metric_decl{.base_de_id           = 0x1a69,
                                                  .name                 = register_name,
                                                  .component            = std::move(component),
                                                  .description          = "test register",
                                                  .unit                 = std::move(unit),
                                                  .base10_unit_modifier = 0,
                                                  .rel_offset           = 0x00};
  astl::scmi::spec::ScmiSpecification spec;
  astl::scmi::spec::Member            member;
  member.count        = 1;
  member.start_offset = 0;
  member.block_size   = 32;
  member.metrics.emplace(register_name, std::move(metric_decl));
  spec.members.push_back(std::move(member));
  return spec;
}

auto MakeResidencyScmiSpec() -> astl::scmi::spec::ScmiSpecification {
  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 64,
       .metrics      = {{"C1_RESIDENCY_COUNTER",
                         {.base_de_id           = 0x1c71,
                          .name                 = "C1_RESIDENCY_COUNTER",
                          .component            = "AP",
                          .description          = "C1 residency",
                          .unit                 = "ticks",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}},
                        {"C3_RESIDENCY_COUNTER",
                         {.base_de_id           = 0x1d82,
                          .name                 = "C3_RESIDENCY_COUNTER",
                          .component            = "AP",
                          .description          = "C3 residency",
                          .unit                 = "ticks",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x10}}}}
  };
  return spec;
}

TEST_CASE("ConfigManager::StaticMetricConfig", "[ConfigManager]") {
  MockMetricManager mock_metric_manager;

  ALLOW_CALL(mock_metric_manager, RegisterMetric(_, _)).RETURN(astl::kInternalNotImplemented);
  // TODO(ASTL-101): Create unit tests for metric manager

  SECTION("Register a valid metric config") {
    // Create a new metric config directly (cannot copy since ExpressionFormula is move-only)
    auto metric_config =
        std::make_unique<astl::MetricConfig>("SoC Temperature", "SoC Temperature in Celsius", ASTL_UNITS_CELSIUS,
                                             ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
                                             astl::CollectorType::SCMI, astl::ScmiOperationBuilder{0x1234});

    REQUIRE(mock_metric_manager.RegisterMetric(std::move(metric_config), {}) == astl::kInternalNotImplemented);
  }

  SECTION("Register an invalid metric config") {
    auto invalid_metric_config = std::make_unique<astl::MetricConfig>(
        "SoC Temperature", "SoC Temperature for abc xyz", ASTL_UNITS_CELSIUS, ASTL_VALUE_UINT64,
        ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE, astl::CollectorType::PROCFS, astl::NullOperationBuilder{});

    REQUIRE(mock_metric_manager.RegisterMetric(std::move(invalid_metric_config), {}) == astl::kInternalNotImplemented);
  }
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("CreateMetricConfig for Residency Metric", "[ConfigManager]") {
  // Create a mock SCMI specification with the residency counter data event IDs
  astl::scmi::spec::ScmiSpecification mock_scmi_spec;
  mock_scmi_spec.members = {
      // Layout for AP cores with C-state counters
      {.count        = 2,  // AP0 and AP1
       .start_offset = 0,
       .block_size   = 64,
       .metrics      = {{"C1_RESIDENCY_COUNTER",
                         {.base_de_id           = 0x1c71,
                          .name                 = "C1_RESIDENCY_COUNTER",
                          .component            = "AP",
                          .description          = "C1 residency",
                          .unit                 = "ticks",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}},
                        {"C3_RESIDENCY_COUNTER",
                         {.base_de_id           = 0x1d82,
                          .name                 = "C3_RESIDENCY_COUNTER",
                          .component            = "AP",
                          .description          = "C3 residency",
                          .unit                 = "ticks",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x10}},
                        {"C6_RESIDENCY_COUNTER",
                         {.base_de_id           = 0x1e93,
                          .name                 = "C6_RESIDENCY_COUNTER",
                          .component            = "AP",
                          .description          = "C6 residency",
                          .unit                 = "ticks",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x20}}}}
  };

  std::vector<const astl::ITarget*> mock_scmi_targets;
  astl::Target                      mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
  mock_scmi_targets.push_back(&mock_target_tlm0);

  // Create a residency metric declaration
  astl::metrics::spec::MetricJsonDeclaration residency_declaration;
  residency_declaration.description = "CPU C-State residency";
  residency_declaration.unit        = "seconds";
  residency_declaration.metric_type = "residency";
  SetScmiCollection(residency_declaration);
  residency_declaration.inferred_state = astl::ResidencyMetricConfig::InferredStateInfo{"Active", "CPU active state"};

  // Set up states configuration
  nlohmann::json states_json;
  states_json["C1"] = {
      {"register",       "C1_RESIDENCY_COUNTER"},
      {"tick_frequency", 1000000.0             },
      {"description",    "C1 idle state"       }
  };
  states_json["C3"] = {
      {"register",       "C3_RESIDENCY_COUNTER"},
      {"tick_frequency", 1000000.0             },
      {"description",    "C3 deep idle state"  }
  };
  states_json["C6"] = {
      {"register",       "C6_RESIDENCY_COUNTER"},
      {"tick_frequency", 1000000.0             },
      {"description",    "C6 power-off state"  }
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
    mock_scmi_spec.members = {
        {.count        = 1,
         .start_offset = 0,
         .block_size   = 32,
         .metrics      = {{"P_STATE",
                           {.base_de_id           = 0x1a69,
                            .name                 = "P_STATE",
                            .component            = "AP",
                            .description          = "P-State",
                            .unit                 = "",
                            .base10_unit_modifier = 0,
                            .rel_offset           = 0x00}}}}
    };

    std::vector<const astl::ITarget*> mock_scmi_targets;
    astl::Target mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
    mock_scmi_targets.push_back(&mock_target_tlm0);

    astl::metrics::spec::MetricJsonDeclaration finite_decl;
    finite_decl.description = "Current CPU performance state (P-state)";
    finite_decl.unit        = "";
    finite_decl.metric_type = "finite_set";
    SetScmiCollection(finite_decl, "P_STATE");

    // json finite_set_values representation: object map of label -> {value, description}
    std::map<std::string, nlohmann::json> finite_json{
        {"P0", {{"value", 0}, {"description", "Performance state 0"}}},
        {"P1", {{"value", 1}, {"description", "Performance state 1"}}},
        {"P2", {{"value", 2}, {"description", "Performance state 2"}}},
        {"P3", {{"value", 3}, {"description", "Performance state 3"}}},
    };
    finite_decl.finite_set_values = finite_json;

    auto metric_configs_result =
        astl::metrics::spec::CreateScmiMetricConfigs("P-State", finite_decl, mock_scmi_spec, mock_scmi_targets);
    REQUIRE(metric_configs_result);
    auto metric_configs_on_targets = std::move(metric_configs_result.value());
    REQUIRE(metric_configs_on_targets.size() == 1);

    std::unordered_map<std::string, astl::FiniteSetMetricConfig*> by_id;
    for (auto& cfg_ptr : metric_configs_on_targets) {
      auto* fs_cfg = dynamic_cast<astl::FiniteSetMetricConfig*>(cfg_ptr.first.get());
      REQUIRE(fs_cfg != nullptr);
      REQUIRE(fs_cfg->Name() == "AP.0.P-State");
      by_id[fs_cfg->Id()] = fs_cfg;
    }
    REQUIRE(by_id.contains("AP.0.P-State__scmi__tlm-0"));

    auto* ap0_cfg = by_id.at("AP.0.P-State__scmi__tlm-0");
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
    mock_scmi_spec.members = {
        {.count        = 1,
         .start_offset = 0,
         .block_size   = 32,
         .metrics      = {{"P_STATE",
                           {.base_de_id           = 0x1a69,
                            .name                 = "P_STATE",
                            .component            = "AP",
                            .description          = "P-State",
                            .unit                 = "",
                            .base10_unit_modifier = 0,
                            .rel_offset           = 0x00}}}}
    };

    std::vector<const astl::ITarget*> mock_scmi_targets;
    astl::Target mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
    mock_scmi_targets.push_back(&mock_target_tlm0);

    astl::metrics::spec::MetricJsonDeclaration bad_decl;
    bad_decl.description = "Bad P-State metric";
    bad_decl.unit        = "";
    bad_decl.metric_type = "finite_set";
    SetScmiCollection(bad_decl, "P_STATE");
    // finite_set_values left empty (optional disengaged)

    auto bad_result =
        astl::metrics::spec::CreateScmiMetricConfigs("P-State", bad_decl, mock_scmi_spec, mock_scmi_targets);
    REQUIRE_FALSE(bad_result.has_value());
    REQUIRE(bad_result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }
}

TEST_CASE("CreateBasicMetricConfigs/CreateFiniteSetMetricConfigs lock formula-before-scaling order",
          "[ConfigManager][FormulaOrder]") {
  SECTION("Basic metric path (CreateBasicMetricConfigs) order is formula then scaling") {
    astl::scmi::spec::ScmiSpecification mock_scmi_spec;
    mock_scmi_spec.members = {
        {.count        = 1,
         .start_offset = 0,
         .block_size   = 32,
         .metrics      = {{"POWER_COUNTER",
                           {.base_de_id           = 0x1200,
                            .name                 = "POWER_COUNTER",
                            .component            = "CPU",
                            .description          = "Power counter",
                            .unit                 = "W",
                            .base10_unit_modifier = -3,
                            .rel_offset           = 0x00}}}}
    };

    std::vector<const astl::ITarget*> mock_scmi_targets;
    astl::Target mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
    mock_scmi_targets.push_back(&mock_target_tlm0);

    astl::metrics::spec::MetricJsonDeclaration basic_decl;
    basic_decl.description = "CPU Power";
    basic_decl.metric_type = "value";
    basic_decl.identifier  = "POWER";
    SetScmiCollection(basic_decl, "POWER_COUNTER");
    basic_decl.formula = nlohmann::json("value + 1");

    auto metric_configs_result =
        astl::metrics::spec::CreateScmiMetricConfigs("CPU Power", basic_decl, mock_scmi_spec, mock_scmi_targets);
    REQUIRE(metric_configs_result.has_value());

    auto metric_configs = std::move(metric_configs_result.value());
    REQUIRE(metric_configs.size() == 1);
    auto* cfg = metric_configs.begin()->first.get();
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->ValueType() == ASTL_VALUE_FLOAT64);
    REQUIRE(cfg->InputValueType() == ASTL_VALUE_UINT64);

    auto applied = astl::ApplyFormula(cfg->GetFormula(), astl::AstlValue{uint64_t{1000}});
    REQUIRE(applied.has_value());
    REQUIRE(std::holds_alternative<double>(applied->value));
    // Locks intended order: (value + 1) then / 1000 => 1.001.
    REQUIRE(std::fabs(std::get<double>(applied->value) - 1.001) < 1e-12);
  }

  SECTION("Finite-set metric path (CreateFiniteSetMetricConfigs) order is formula then scaling") {
    astl::scmi::spec::ScmiSpecification mock_scmi_spec;
    mock_scmi_spec.members = {
        {.count        = 1,
         .start_offset = 0,
         .block_size   = 32,
         .metrics      = {{"P_STATE",
                           {.base_de_id           = 0x1300,
                            .name                 = "P_STATE",
                            .component            = "AP",
                            .description          = "P-state",
                            .unit                 = "",
                            .base10_unit_modifier = -1,
                            .rel_offset           = 0x00}}}}
    };

    std::vector<const astl::ITarget*> mock_scmi_targets;
    astl::Target mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
    mock_scmi_targets.push_back(&mock_target_tlm0);

    astl::metrics::spec::MetricJsonDeclaration finite_decl;
    finite_decl.description = "Current CPU performance state";
    finite_decl.metric_type = "finite_set";
    SetScmiCollection(finite_decl, "P_STATE");
    finite_decl.formula           = nlohmann::json("value + 1");
    finite_decl.finite_set_values = std::map<std::string, nlohmann::json>{
        {"P0", {{"value", 0}, {"description", "Performance state 0"}}},
        {"P1", {{"value", 1}, {"description", "Performance state 1"}}},
    };

    auto metric_configs_result =
        astl::metrics::spec::CreateScmiMetricConfigs("P-State", finite_decl, mock_scmi_spec, mock_scmi_targets);
    REQUIRE(metric_configs_result.has_value());

    auto metric_configs = std::move(metric_configs_result.value());
    REQUIRE(metric_configs.size() == 1);
    auto* finite_cfg = dynamic_cast<astl::FiniteSetMetricConfig*>(metric_configs.begin()->first.get());
    REQUIRE(finite_cfg != nullptr);
    REQUIRE(finite_cfg->ValueType() == ASTL_VALUE_FLOAT64);
    REQUIRE(finite_cfg->InputValueType() == ASTL_VALUE_UINT64);

    auto applied = astl::ApplyFormula(finite_cfg->GetFormula(), astl::AstlValue{uint64_t{20}});
    REQUIRE(applied.has_value());
    REQUIRE(std::holds_alternative<double>(applied->value));
    // Locks intended order: (value + 1) then / 10 => 2.1.
    REQUIRE(std::fabs(std::get<double>(applied->value) - 2.1) < 1e-12);
  }
}

TEST_CASE("CreateScmiMetricConfigs validates metric declarations and unsupported combinations",
          "[ConfigManager][Validation]") {
  astl::Target                      mock_target_tlm0("tlm-0", "dummy test target", astl::CollectorType::SCMI, nullptr);
  std::vector<const astl::ITarget*> mock_scmi_targets{&mock_target_tlm0};

  SECTION("Unknown metric type returns NOT_IMPLEMENTED") {
    auto basic_spec = MakeSingleRegisterScmiSpec("TEMP_PRESENT", "AP", "celsius");

    astl::metrics::spec::MetricJsonDeclaration decl;
    decl.description = "Unknown metric type";
    decl.metric_type = "mystery";
    SetScmiCollection(decl, "TEMP_PRESENT");

    auto result = astl::metrics::spec::CreateScmiMetricConfigs("Unknown Metric", decl, basic_spec, mock_scmi_targets);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == astl::kInternalNotImplemented);
  }

  SECTION("Basic metric with unsupported collector returns NOT_IMPLEMENTED") {
    auto basic_spec = MakeSingleRegisterScmiSpec("TEMP_PRESENT", "AP", "celsius");

    astl::metrics::spec::MetricJsonDeclaration decl;
    decl.description         = "Libsensors metric";
    decl.metric_type         = "value";
    decl.collection.protocol = "libsensors";
    decl.collection.raw_json = nlohmann::json{
        {"protocol", "libsensors"  },
        {"register", "TEMP_PRESENT"}
    };

    auto result =
        astl::metrics::spec::CreateScmiMetricConfigs("Libsensors Metric", decl, basic_spec, mock_scmi_targets);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == astl::kInternalNotImplemented);
  }

  SECTION("Finite-set metric rejects duplicate values across labels") {
    auto finite_spec = MakeSingleRegisterScmiSpec("P_STATE");

    astl::metrics::spec::MetricJsonDeclaration decl;
    decl.description = "Duplicate finite-set values";
    decl.metric_type = "finite_set";
    SetScmiCollection(decl, "P_STATE");
    decl.finite_set_values = std::map<std::string, nlohmann::json>{
        {"P0", {{"value", 0}, {"description", "Performance state 0"}}},
        {"P1", {{"value", 0}, {"description", "Duplicate value"}}    },
    };

    auto result = astl::metrics::spec::CreateScmiMetricConfigs("P-State", decl, finite_spec, mock_scmi_targets);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Finite-set metric rejects unsupported JSON value types") {
    auto finite_spec = MakeSingleRegisterScmiSpec("P_STATE");

    astl::metrics::spec::MetricJsonDeclaration decl;
    decl.description = "Bad finite-set value type";
    decl.metric_type = "finite_set";
    SetScmiCollection(decl, "P_STATE");
    decl.finite_set_values = std::map<std::string, nlohmann::json>{
        {"P0", {{"value", "zero"}, {"description", "String value should fail"}}},
    };

    auto result = astl::metrics::spec::CreateScmiMetricConfigs("P-State", decl, finite_spec, mock_scmi_targets);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
  }

  SECTION("Residency metric rejects states missing tick_frequency") {
    auto residency_spec = MakeResidencyScmiSpec();

    astl::metrics::spec::MetricJsonDeclaration decl;
    decl.description    = "CPU C-State residency";
    decl.unit           = "seconds";
    decl.metric_type    = "residency";
    decl.inferred_state = astl::ResidencyMetricConfig::InferredStateInfo{"Active", "CPU active state"};
    SetScmiCollection(decl);
    decl.states = std::map<std::string, nlohmann::json>{
        {"C1", {{"register", "C1_RESIDENCY_COUNTER"}, {"description", "C1 idle state"}}                               },
        {"C3", {{"register", "C3_RESIDENCY_COUNTER"}, {"tick_frequency", 1000000.0}, {"description", "C3 idle state"}}},
    };

    auto result = astl::metrics::spec::CreateScmiMetricConfigs("C-State", decl, residency_spec, mock_scmi_targets);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
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

TEST_CASE("ConfigurationManager load-file override round-trips and clears", "[ConfigManager][Paths]") {
  astl::ConfigurationManager::SetLoadFilePathOverride(std::nullopt);
  REQUIRE_FALSE(astl::ConfigurationManager::GetLoadFilePathOverride().has_value());

  const auto override_path = std::filesystem::path{"/tmp/test-session.astl"};
  astl::ConfigurationManager::SetLoadFilePathOverride(override_path);

  auto current_override = astl::ConfigurationManager::GetLoadFilePathOverride();
  REQUIRE(current_override.has_value());
  REQUIRE(current_override.value() == override_path);

  astl::ConfigurationManager::SetLoadFilePathOverride(std::nullopt);
  REQUIRE_FALSE(astl::ConfigurationManager::GetLoadFilePathOverride().has_value());
}

// ===================== Identifier Parsing Tests =====================
TEST_CASE("ParseConfiguration missing identifier defaults to unknown/UNKNOWN", "[ConfigManager][Identifier]") {
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
  REQUIRE(cpu_power_metric.identifier == "unknown");  // default string

  // Build a metric config to verify enum mapping
  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"CPU_POWER",
                         {.base_de_id           = 0xabcd,
                          .name                 = "CPU_POWER",
                          .component            = "AP",
                          .description          = "CPU Power",
                          .unit                 = "W",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };
  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);
  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("CPU Power", cpu_power_metric, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Name() == "AP.0.CPU Power");
  REQUIRE(metric_configs.begin()->first->Id() == "AP.0.CPU Power__scmi__tlm-0");
  REQUIRE(metric_configs.begin()->first->Identifier() == ASTL_METRIC_IDENTIFIER_UNKNOWN);
}

TEST_CASE("ParseConfiguration valid identifier string maps to enum", "[ConfigManager][Identifier]") {
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
        "identifier": "TEMPERATURE",
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
  REQUIRE(soc_temp_metric.identifier == "TEMPERATURE");

  // Build a metric config to verify enum mapping
  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"SOC_TEMP",
                         {.base_de_id           = 0xdcba,
                          .name                 = "SOC_TEMP",
                          .component            = "AP",
                          .description          = "SoC Temp",
                          .unit                 = "C",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };
  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);
  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("SoC Temperature", soc_temp_metric, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Identifier() == ASTL_METRIC_IDENTIFIER_TEMPERATURE);
  REQUIRE(metric_configs.begin()->first->Description() == "Temperature in Celsius [component: AP instance: 0]");
}

TEST_CASE("CreateScmiMetricConfigs falls back to generated description when JSON description is empty",
          "[ConfigManager][Identifier]") {
  astl::metrics::spec::MetricJsonDeclaration metric_declaration;
  metric_declaration.description = "";
  metric_declaration.metric_type = "value";
  metric_declaration.identifier  = "TEMPERATURE";
  SetScmiCollection(metric_declaration, "SOC_TEMP");

  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"SOC_TEMP",
                         {.base_de_id           = 0xdcba,
                          .name                 = "SOC_TEMP",
                          .component            = "AP",
                          .description          = "SoC Temp",
                          .unit                 = "C",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };

  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);

  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("SoC Temperature", metric_declaration, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Description() ==
          "Temperature reading for SoC Temperature [component: AP instance: 0]");
}

TEST_CASE("CreateScmiMetricConfigs scopes SCMI metric ids per target", "[ConfigManager][Identifier]") {
  astl::metrics::spec::MetricJsonDeclaration metric_declaration;
  metric_declaration.description              = "CPU power consumption";
  metric_declaration.metric_type              = "value";
  metric_declaration.identifier               = "POWER";
  metric_declaration.collection.protocol      = "scmi";
  metric_declaration.collection.register_name = "CPU_POWER";

  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"CPU_POWER",
                         {.base_de_id           = 0xabcd,
                          .name                 = "CPU_POWER",
                          .component            = "AP",
                          .description          = "CPU Power",
                          .unit                 = "W",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };

  astl::Target target_tlm0("scmi-mocksysfs-tlm-0", "test target 0", astl::CollectorType::SCMI, nullptr, std::nullopt,
                           std::string{"tlm-0"});
  astl::Target target_tlm1("scmi-mocksysfs-tlm-1", "test target 1", astl::CollectorType::SCMI, nullptr, std::nullopt,
                           std::string{"tlm-1"});
  std::vector<const astl::ITarget*> targets{&target_tlm0, &target_tlm1};

  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("CPU Power", metric_declaration, spec, targets);
  REQUIRE(metric_configs_result);

  std::unordered_set<std::string> metric_ids;
  for (auto& [config, scoped_targets] : *metric_configs_result) {
    REQUIRE(config->Name() == "AP.0.CPU Power");
    REQUIRE(scoped_targets.size() == 1);
    REQUIRE(metric_ids.insert(config->Id()).second);
  }

  REQUIRE(metric_ids.contains("AP.0.CPU Power__scmi__tlm-0"));
  REQUIRE(metric_ids.contains("AP.0.CPU Power__scmi__tlm-1"));
}

TEST_CASE("CreateScmiMetricConfigs maps Count units to ASTL_UNITS_COUNT", "[ConfigManager][Units]") {
  astl::metrics::spec::MetricJsonDeclaration metric_declaration;
  metric_declaration.description = "Number of thermal throttling events";
  metric_declaration.metric_type = "delta";
  metric_declaration.identifier  = "COUNT";
  SetScmiCollection(metric_declaration, "THROTTLE_EVENTS");

  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"THROTTLE_EVENTS",
                         {.base_de_id           = 0x8C3D,
                          .name                 = "THROTTLE_EVENTS",
                          .component            = "CORE",
                          .description          = "Mock sysfs cpu 1 throttle events",
                          .unit                 = "Count",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };

  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);

  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("Throttle Counts", metric_declaration, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Units() == ASTL_UNITS_COUNT);
}

TEST_CASE("CreateScmiMetricConfigs allows output-unit override with formula scaling", "[ConfigManager][Units]") {
  astl::metrics::spec::MetricJsonDeclaration metric_declaration;
  metric_declaration.description = "Frequency reading for FREQUENCY_PRESENT";
  metric_declaration.unit        = "MHz";
  metric_declaration.formula     = nlohmann::json("value / 1000");
  metric_declaration.metric_type = "value";
  metric_declaration.identifier  = "FREQUENCY";
  SetScmiCollection(metric_declaration, "FREQUENCY_PRESENT");

  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"FREQUENCY_PRESENT",
                         {.base_de_id           = 0x0120,
                          .name                 = "FREQUENCY_PRESENT",
                          .component            = "CORE",
                          .description          = "Instantaneous operational frequency",
                          .unit                 = "Hz",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };

  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);

  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("FREQUENCY_PRESENT", metric_declaration, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE(metric_configs.size() == 1);

  auto* cfg = metric_configs.begin()->first.get();
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->Units() == ASTL_UNITS_MHZ);
  REQUIRE(cfg->ValueType() == ASTL_VALUE_UINT64);
  REQUIRE(cfg->InputValueType() == ASTL_VALUE_UINT64);

  auto applied = astl::ApplyFormula(cfg->GetFormula(), astl::AstlValue{uint64_t{1000}});
  REQUIRE(applied.has_value());
  REQUIRE(std::holds_alternative<uint64_t>(applied->value));
  REQUIRE(std::get<uint64_t>(applied->value) == 1);
}

TEST_CASE("ParseConfiguration invalid identifier string maps to UNKNOWN", "[ConfigManager][Identifier]") {
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
        "identifier": "THIS_IS_NOT_A_VALID_VALUE",
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
  REQUIRE(gpu_power_metric.identifier == "THIS_IS_NOT_A_VALID_VALUE");

  // Build a metric config to verify enum mapping
  astl::scmi::spec::ScmiSpecification spec;
  spec.members = {
      {.count        = 1,
       .start_offset = 0,
       .block_size   = 32,
       .metrics      = {{"GPU_POWER",
                         {.base_de_id           = 0x1234,
                          .name                 = "GPU_POWER",
                          .component            = "AP",
                          .description          = "GPU Power",
                          .unit                 = "W",
                          .base10_unit_modifier = 0,
                          .rel_offset           = 0x00}}}}
  };
  std::vector<const astl::ITarget*> targets;
  astl::Target                      test_target("tlm-0", "test target", astl::CollectorType::SCMI, nullptr);
  targets.push_back(&test_target);
  auto metric_configs_result =
      astl::metrics::spec::CreateScmiMetricConfigs("GPU Power", gpu_power_metric, spec, targets);
  REQUIRE(metric_configs_result);
  auto metric_configs = std::move(metric_configs_result.value());
  REQUIRE_FALSE(metric_configs.empty());
  REQUIRE(metric_configs.begin()->first->Identifier() == ASTL_METRIC_IDENTIFIER_UNKNOWN);
}
