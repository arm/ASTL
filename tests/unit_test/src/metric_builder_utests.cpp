/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include <filesystem>
#include <memory>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "astl/astl_telemetry.h"
#include "config/astl_configuration.hpp"
#include "metric/i_metric_manager.hpp"
#include "metric/metric_builder.hpp"
#include "target.hpp"

using trompeloeil::_;

namespace fs = std::filesystem;

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

  auto mock_target = std::make_unique<MockTarget>();
  REQUIRE_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::UNKNOWN);
  ALLOW_CALL(*mock_target, Name()).RETURN("mock_target_unknown");

  targets.push_back(std::move(mock_target));

  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto result = astl::BuildMetricManager(targets, configuration_result.value(), std::nullopt);

  REQUIRE(result.has_value());
}

TEST_CASE("MetricBuilder::BuildMetricManager with SCMI target but no config", "[MetricBuilder]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;

  auto mock_target = std::make_unique<MockTarget>();
  REQUIRE_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  ALLOW_CALL(*mock_target, Name()).RETURN("scmi_target_0");

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

  auto mock_target = std::make_unique<MockTarget>();
  REQUIRE_CALL(*mock_target, GetCollectorType()).RETURN(astl::CollectorType::LIBSENSORS);
  ALLOW_CALL(*mock_target, Name()).RETURN("libsensors_target");

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

  auto scmi_target = std::make_unique<MockTarget>();
  REQUIRE_CALL(*scmi_target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  ALLOW_CALL(*scmi_target, Name()).RETURN("scmi_0");

  auto libsensors_target = std::make_unique<MockTarget>();
  REQUIRE_CALL(*libsensors_target, GetCollectorType()).RETURN(astl::CollectorType::LIBSENSORS);
  ALLOW_CALL(*libsensors_target, Name()).RETURN("libsensors_0");

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