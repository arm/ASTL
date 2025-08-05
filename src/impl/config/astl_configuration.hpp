/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#ifndef ASTL_CONFIGURATION_HPP_
#define ASTL_CONFIGURATION_HPP_

#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "astl/astl_errors.h"
#include "config/scmi_specification_json.hpp"
#include "metric/metric_config.hpp"

using json = nlohmann::json;

namespace astl {

struct MetricJsonDeclaration {
  std::string description;          //!< Description of the metric
  std::string register_name;        //!< Register name associated with the metric
  std::string unit;                 //!< Unit of measurement for the metric
  std::string metric_type;          //!< Type of metric (e.g., value, delta, rate)
  std::string collection_protocol;  //!< Collector type (e.g., scmi)
};

/** @brief Overall configuration for the ASTL library */
struct AstlConfiguration {
  /** @brief scmi_sysfs_telemetry_root_override is an optional path to replace "/tmp/fuse/scmi/scmi_telemetry"
   *         This is a placeholder example of something that _could_ be configured.
   *         subject to change, not currently modified.
   */
  std::optional<std::filesystem::path> scmi_sysfs_telemetry_root_path;

  /** @brief collection of metric declarations for ASTL to present to user */
  std::map<std::string, MetricJsonDeclaration> metric_declarations;

  /** @brief Override path for configuration file containing SCMI metric definitions */
  std::optional<std::filesystem::path> scmi_specification_path;
};

auto ParseConfiguration(std::istream& configuration_data) -> std::expected<AstlConfiguration, astl_status_code>;

/**
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_declaration The MetricJsonDeclaration object to convert
 * @param layout The Scmi layout specification containing the Data Event IDs from platform json spec
 */
auto CreateMetricConfig(std::string_view metric_name, MetricJsonDeclaration const& metric_declaration,
                        scmi::Layout const& layout) -> std::unique_ptr<MetricConfig>;

}  // namespace astl

#endif  // ASTL_CONFIGURATION_HPP_