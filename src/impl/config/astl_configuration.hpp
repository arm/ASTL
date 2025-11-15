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
#include "astl/astl_telemetry.h"
#include "common/metric_config.hpp"
#include "config/scmi_specification_json.hpp"
#include "target.hpp"

using json = nlohmann::json;

namespace astl {

struct MetricJsonDeclaration {
  MetricJsonDeclaration() = default;

  std::string                             description;    //!< Description of the metric
  std::string                             register_name;  //!< Register name associated with the metric
  std::string                             offset;  //!< Register offset - exact meaning depends on collection_protocol
  std::string                             unit;    //!< Unit of measurement for the metric
  std::string                             metric_type;  //!< Type of metric (e.g., value, delta, rate)
  std::string                             category;  //!< Categories include things like Temperature, Power, Count, etc.
  std::optional<std::vector<std::string>> metric_groups;        //!< Groups this metric is associated with
  std::string                             collection_protocol;  //!< Collector type (e.g., scmi, libsensors)

  // Residency-specific fields
  std::optional<std::string> inferred_state;                    //!< Name of inferred state (for residency metrics)
  std::optional<std::map<std::string, nlohmann::json>> states;  //!< State definitions (for residency metrics)

  // Finite set specific fields
  std::optional<std::vector<nlohmann::json>> finite_set_values;  //!< Valid values for finite set metrics
};

/** @brief Overall configuration for the ASTL library */
struct AstlConfiguration {
  AstlConfiguration() = default;

  /** @brief scmi_sysfs_telemetry_root_override is an optional path to replace "/sys/fs/arm_telemetry"
   *         This is a placeholder example of something that _could_ be configured.
   *         subject to change, not currently modified.
   */
  std::optional<std::filesystem::path> scmi_sysfs_telemetry_root_path;

  /** @brief collection of metric declarations for ASTL to present to user */
  std::unordered_map<std::string, MetricJsonDeclaration> metric_declarations;  // unordered for faster lookup

  /** @brief Override path for configuration file containing SCMI metric definitions */
  std::optional<std::filesystem::path> scmi_specification_path;
};

[[nodiscard]] auto ParseConfiguration(std::string_view configuration_data)
    -> std::expected<AstlConfiguration, astl_status_code>;

[[nodiscard]] auto ParseConfiguration(std::istream& configuration_data)
    -> std::expected<AstlConfiguration, astl_status_code>;

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType>;

/**
 * @brief helper function to create a MetricConfig object from a MetricJsonDeclaration and ScmiSpecification
 * @param metric_key_name    The string key from scmi specification json in the layout.members.<member>. entries list,
 * e.g. 'ENERGY_COUNTER'
 * @param metric_declaration The MetricJsonDeclaration json definition of this type of metric from the astl
 * configuration json. adds info like units on how to interpret the metrics
 * @param scmi_spec          The scmi json spec, especially scmi::Layout for this platform
 * @param targets A vector of ITarget pointers represending the detected SCMI targets on this platform
 *
 */
[[nodiscard]] auto CreateScmiMetricConfigs(std::string_view                   metric_key_name,
                                           MetricJsonDeclaration const&       metric_declaration,
                                           scmi::ScmiSpecification const&     scmi_spec,
                                           std::vector<const ITarget*> const& scmi_targets)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code>;

}  // namespace astl

#endif  // ASTL_CONFIGURATION_HPP_
