// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl/astl_telemetry.h"
#include "common/capabilities.hpp"
#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/metric_builder.hpp"
#include "metric/metric_manager.hpp"
#include "metric/sampled_value_metric.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "target.hpp"
#include "topology/scmi_target.hpp"

using trompeloeil::_;

namespace fs = std::filesystem;

namespace {

void WriteTextFile(const fs::path& path, std::string_view contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  REQUIRE(!ec);

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  REQUIRE(out.good());
  out << contents;
  REQUIRE(out.good());
}

auto MakeConfigurationForTestRoot(const fs::path& config_root) -> astl::AstlConfiguration {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());

  auto configuration                   = configuration_result.value();
  configuration.config_dir_path        = config_root;
  configuration.metrics_dir_path       = config_root / "metrics";
  configuration.groups_dir_path        = config_root / "groups";
  configuration.scmi_specification_dir = config_root / "scmi" / "public";
  return configuration;
}

void WriteMinimalScmiFixture(const fs::path& config_root) {
  WriteTextFile(config_root / "scmi" / "public" / "repometa.json", R"json({
  "last_updated": "2026-03-16",
  "uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2026-03-16",
      "description": "Unit test SCMI specification",
      "specification_file": "unit/test_scmi.json"
    }
  }
})json");

  WriteTextFile(config_root / "metrics" / "platform_lookup.json", R"json({
  "last_updated": "2026-03-16",
  "scmi_uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2026-03-16",
      "description": "Unit test metric declarations",
      "metrics_file": "unit/test_metrics.json",
      "name": "{telemetry_subdirectory}"
    }
  }
})json");

  WriteTextFile(config_root / "scmi" / "public" / "unit" / "test_scmi.json", R"json({
  "_type": "metrics_specification",
  "document": {
    "timestamp": "2026-03-16",
    "copyright": "Copyright 2026 Arm Ltd.",
    "confidential": false,
    "quality": "Test",
    "license": "Apache-2.0",
    "description": "Unit test telemetry data capture format layout specification"
  },
  "uuid": "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/14",
  "description": "Unit test SCMI spec",
  "tdcf_instance_id": "[7:0]",
  "chiplet_id": "[15:8]",
  "size": 32,
  "members": [
    {
      "count": 1,
      "start_offset": 0,
      "block_size": 32,
      "metrics": {
        "ENERGY_COUNTER": {
          "description": "Unit test energy counter",
          "type": "Gauge",
          "unit": "W",
          "base10_unit_modifier": 0,
          "name": "ENERGY_COUNTER",
          "component": "SOC",
          "base_de_id": "0x00009E4F",
          "rel_offset": "0x0000"
        }
      }
    }
  ]
})json");

  WriteTextFile(config_root / "metrics" / "unit" / "test_metrics.json", R"json({
  "document": {
    "confidential": false
  },
  "metrics": {
    "SoC Power": {
      "description": "Unit test SoC power metric",
      "unit": "W",
      "metric_type": "value",
      "identifier": "POWER",
      "collection": {
        "register": "ENERGY_COUNTER",
        "protocol": "scmi"
      }
    }
  }
})json");

  WriteTextFile(config_root / "groups" / "metric_groups.json", R"json({
  "metric_groups": {
    "power": {
      "description": "Unit test power metrics"
    }
  }
})json");
}

void WriteSerializedMetricManagerCache(const fs::path& cache_dir, const astl::ITarget* target) {
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);

  std::vector<astl::CollectorCapability> collector_caps;
  collector_caps.emplace_back(astl::CollectorType::SCMI);
  std::vector<astl::SystemCapability> system_caps;
  system_caps.emplace_back();
  astl::Capabilities caps{std::move(collector_caps), std::move(system_caps)};

  astl::MetricManager metric_manager{caps};
  auto                cfg = std::make_unique<astl::MetricConfig>("test_metric", "unit-test metric", ASTL_UNITS_CELSIUS,
                                                                 ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
                                                                 astl::CollectorType::SCMI, astl::NullOperationBuilder{});
  REQUIRE(cfg != nullptr);

  auto metric = std::make_unique<astl::SampledValueMetric>(cfg.get(), target, nullptr);
  REQUIRE(metric != nullptr);
  astl::MetricManagerTestAccessor::InjectMetric(metric_manager, std::move(metric), std::move(cfg), target);

  std::ofstream metric_file(cache_dir / astl::kMetricManagerFileName, std::ios::binary | std::ios::out);
  REQUIRE(metric_file.good());
  REQUIRE(astl::ProtobufSerDes::Serialize(metric_manager, metric_file) == ASTL_STATUS_SUCCESS);
}

}  // namespace

TEST_CASE("MetricBuilder::BuildMetricManager with empty targets", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto                                        configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  auto result = astl::BuildMetricManager(targets, configuration, std::nullopt);

  REQUIRE(result.has_value());
  REQUIRE(result.value() != nullptr);
}

TEST_CASE("MetricBuilder::BuildMetricManager with unknown collector type", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto                     mock_target      = std::make_unique<MockTarget>();
  static const std::string mock_target_name = "mock_target_unknown";
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target, Name()).RETURN(mock_target_name);

  targets.push_back(std::move(mock_target));

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto result = astl::BuildMetricManager(targets, configuration_result.value(), std::nullopt);

  REQUIRE(result.has_value());
}

TEST_CASE("MetricBuilder::BuildMetricManager with SCMI target but no config", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto                     mock_target      = std::make_unique<MockTarget>();
  static const std::string mock_target_name = "scmi_target_0";
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  ALLOW_CALL(*mock_target, Name()).RETURN(mock_target_name);

  targets.push_back(std::move(mock_target));

  auto create_config_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(create_config_result.has_value());
  auto config = create_config_result.value();
  auto result = astl::BuildMetricManager(targets, config, std::nullopt);
  // Note this will pass only if publish_data.sh has been run to move config files to the expected location
  // like ./build/debug/lib/data
  // REQUIRE(result.has_value());
}

TEST_CASE("MetricBuilder::BuildMetricManager with libsensors target", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto                     mock_target      = std::make_unique<MockTarget>();
  static const std::string mock_target_name = "libsensors_target";
  ALLOW_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::LIBSENSORS);
  ALLOW_CALL(*mock_target, Name()).RETURN(mock_target_name);

  targets.push_back(std::move(mock_target));

  auto create_config_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(create_config_result.has_value());
  auto config = create_config_result.value();
  auto result = astl::BuildMetricManager(targets, config, std::nullopt);

  // MockTarget doesn't implement LibsensorsTarget, so BuildMetricManager should fail right now
  REQUIRE(!result.has_value());
}

TEST_CASE("MetricBuilder::BuildMetricManager with mixed collector types", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto                     scmi_target      = std::make_unique<MockTarget>();
  static const std::string scmi_target_name = "scmi_0";
  ALLOW_CALL(*scmi_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  ALLOW_CALL(*scmi_target, Name()).RETURN(scmi_target_name);

  auto                     libsensors_target      = std::make_unique<MockTarget>();
  static const std::string libsensors_target_name = "libsensors_0";
  ALLOW_CALL(*libsensors_target, GetCollectorType()).RETURN(astl::CollectorType::LIBSENSORS);
  ALLOW_CALL(*libsensors_target, Name()).RETURN(libsensors_target_name);

  targets.push_back(std::move(scmi_target));
  targets.push_back(std::move(libsensors_target));

  auto create_config_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(create_config_result.has_value());
  auto config = create_config_result.value();
  auto result = astl::BuildMetricManager(targets, config, std::nullopt);

  // MockTarget doesn't implement LibsensorsTarget, so BuildMetricManager should fail right now
  REQUIRE(!result.has_value());
}

TEST_CASE("MetricBuilder::BuildMetricManagerFromASTLFile with nonexistent path", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto create_config_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(create_config_result.has_value());
  auto configuration           = create_config_result.value();
  configuration.load_file_path = "/nonexistent/path/to/dir";

  fs::path nonexistent_cache = "/tmp/nonexistent_cache_dir_12345";

  auto result = astl::BuildMetricManager(targets, configuration, nonexistent_cache);

  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("MetricBuilder::BuildMetricManager uses JSON descriptions for SCMI counters", "[MetricBuilder]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  const fs::path config_root = fs::temp_directory_path() / "astl_metric_builder_json_descriptions";
  TempFileGuard  config_guard(config_root);
  WriteMinimalScmiFixture(config_root);

  configuration = MakeConfigurationForTestRoot(config_root);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  auto        scmi_target = std::make_unique<astl::ScmiTarget>("scmi_tlm-0", "test target", "tlm-0", nullptr,
                                                               "0xCAFEBABECAFEBABECAFEBABEBEEF0000");
  const auto* target_ptr  = scmi_target.get();
  targets.push_back(std::move(scmi_target));

  auto metric_manager_or_error = astl::BuildMetricManager(targets, configuration, std::nullopt);
  REQUIRE(metric_manager_or_error.has_value());

  auto& metric_manager    = *metric_manager_or_error.value();
  auto  counters_or_error = metric_manager.GetAvailableCounters(target_ptr);
  REQUIRE(counters_or_error.has_value());
  REQUIRE_FALSE(counters_or_error->empty());

  std::vector<std::string> counter_descriptions;
  for (const auto* const counter : *counters_or_error) {
    astl_counter_props_t properties{};
    properties.size = sizeof(astl_counter_props_t);
    REQUIRE(metric_manager.GetCounterProperties(counter, &properties) == ASTL_STATUS_SUCCESS);
    REQUIRE(properties.description != nullptr);
    counter_descriptions.emplace_back(properties.description);
  }

  REQUIRE(std::ranges::find(counter_descriptions, "Unit test SoC power metric") != counter_descriptions.end());
  REQUIRE(std::ranges::find(counter_descriptions, "Underlying counter for SoC Power") == counter_descriptions.end());
}

TEST_CASE("MetricBuilder::BuildMetricManager rejects load_file_path without cache dir", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  auto result = astl::BuildMetricManager(targets, configuration, std::nullopt);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("MetricBuilder::BuildMetricManagerFromASTLFile fails when metric manager file is missing",
          "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  const fs::path  cache_dir = fs::temp_directory_path() / "astl_metric_builder_missing_metric_manager";
  TempFileGuard   cache_guard(cache_dir);
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);
  fs::remove(cache_dir / astl::kMetricManagerFileName, ec);

  auto result = astl::BuildMetricManager(targets, configuration, cache_dir);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("MetricBuilder::BuildMetricManagerFromASTLFile fails on corrupt metric manager protobuf", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  const fs::path cache_dir = fs::temp_directory_path() / "astl_metric_builder_corrupt_metric_manager";
  TempFileGuard  cache_guard(cache_dir);
  WriteTextFile(cache_dir / astl::kMetricManagerFileName, "not a protobuf");

  auto result = astl::BuildMetricManager(targets, configuration, cache_dir);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("MetricBuilder::BuildMetricManagerFromASTLFile rebuilds a serialized metric manager", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "unit-test target", "tlm-0"));

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  const fs::path cache_dir = fs::temp_directory_path() / "astl_metric_builder_valid_metric_manager";
  TempFileGuard  cache_guard(cache_dir);
  WriteSerializedMetricManagerCache(cache_dir, targets[0].get());

  auto result = astl::BuildMetricManager(targets, configuration, cache_dir);

  REQUIRE(result.has_value());
  REQUIRE(result.value() != nullptr);

  auto metrics_or_error = result.value()->GetAvailableMetrics(targets[0].get());
  REQUIRE(metrics_or_error.has_value());
  REQUIRE(metrics_or_error->size() == 1);
}

TEST_CASE("MetricBuilder::BuildMetricManager registers SCMI metrics from temporary config fixture", "[MetricBuilder]") {
  const fs::path config_root = fs::temp_directory_path() / "astl_metric_builder_scmi_fixture";
  TempFileGuard  config_guard(config_root);
  WriteMinimalScmiFixture(config_root);

  auto configuration = MakeConfigurationForTestRoot(config_root);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "unit-test target", "tlm-0", nullptr,
                                                       "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000"));

  auto result = astl::BuildMetricManager(targets, configuration, std::nullopt);

  REQUIRE(result.has_value());
  REQUIRE(result.value() != nullptr);

  auto metrics_or_error = result.value()->GetAvailableMetrics(targets[0].get());
  REQUIRE(metrics_or_error.has_value());
  REQUIRE(metrics_or_error->size() == 1);

  auto counters_or_error = result.value()->GetAvailableCounters(targets[0].get());
  REQUIRE(counters_or_error.has_value());
  REQUIRE(counters_or_error->size() == 1);

  astl_metric_props_t metric_props{};
  metric_props.size = sizeof(astl_metric_props_t);
  REQUIRE(result.value()->GetProperties((*metrics_or_error)[0], &metric_props) == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string{metric_props.name} == "SoC Power");

  astl_counter_props_t counter_props{};
  counter_props.size = sizeof(astl_counter_props_t);
  REQUIRE(result.value()->GetCounterProperties((*counters_or_error)[0], &counter_props) == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string{counter_props.name} == "ENERGY_COUNTER");
}

TEST_CASE("MetricBuilder::BuildMetricManager applies SCMI target name template from platform lookup",
          "[MetricBuilder]") {
  const fs::path config_root = fs::temp_directory_path() / "astl_metric_builder_scmi_target_name_fixture";
  TempFileGuard  config_guard(config_root);
  WriteMinimalScmiFixture(config_root);

  auto configuration = MakeConfigurationForTestRoot(config_root);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ScmiTarget>("scmi_tlm-0", "unit-test target", "tlm-0", nullptr,
                                                       "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000"));

  auto result = astl::BuildMetricManager(targets, configuration, std::nullopt);

  REQUIRE(result.has_value());
  REQUIRE(targets[0]->Name() == "tlm-0");
}

TEST_CASE("MetricBuilder::BuildMetricManager loads metric group metadata without group confidential flag",
          "[MetricBuilder]") {
  const fs::path config_root = fs::temp_directory_path() / "astl_metric_builder_group_fixture";
  TempFileGuard  config_guard(config_root);
  WriteMinimalScmiFixture(config_root);

  WriteTextFile(config_root / "metrics" / "unit" / "test_metrics.json", R"json({
  "document": {
    "confidential": false
  },
  "metrics": {
    "SoC Power": {
      "description": "Unit test SoC power metric",
      "unit": "W",
      "metric_type": "value",
      "identifier": "POWER",
      "metric_groups": ["power"],
      "collection": {
        "register": "ENERGY_COUNTER",
        "protocol": "scmi"
      }
    }
  }
})json");

  auto configuration = MakeConfigurationForTestRoot(config_root);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ScmiTarget>("tlm-0", "unit-test target", "tlm-0", nullptr,
                                                       "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000"));

  auto result = astl::BuildMetricManager(targets, configuration, std::nullopt);

  REQUIRE(result.has_value());
  REQUIRE(result.value() != nullptr);

  auto groups_or_error = result.value()->GetMetricGroups(targets[0].get());
  REQUIRE(groups_or_error.has_value());
  REQUIRE(groups_or_error->size() == 1);

  astl_metric_group_props_t group_props{};
  group_props.size = sizeof(astl_metric_group_props_t);
  REQUIRE(result.value()->GetMetricGroupProperties((*groups_or_error)[0], &group_props) == ASTL_STATUS_SUCCESS);
  REQUIRE(std::string{group_props.name} == "power");
  REQUIRE(std::string{group_props.description} == "Unit test power metrics");
}
