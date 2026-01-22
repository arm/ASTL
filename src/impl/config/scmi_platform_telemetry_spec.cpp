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

#include "config/scmi_platform_telemetry_spec.hpp"

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config/metric_json_declaration.hpp"
#include "operation/scmi_read_operation.hpp"

using json = nlohmann::json;

namespace astl::scmi::spec {

/**
 * @brief Helper to determine if the given units from the metric declaration/selection and the
 * SCMI spec match, returning the resolved units if so.
 *
 * If both are specified, and they don't match, this returns a std::nullopt, meaning this
 * specified scmi metric doesn't match the requested metric and should be skipped
 */
inline auto GetUnitsIfCompatible(std::string_view metric_unit, std::string_view spec_unit)
    -> std::optional<astl_units_t> {
  // if either unit is empty, we consider it a match (no restriction), so use the other.
  if (metric_unit.empty()) {
    return ParseUnits(spec_unit);
  }
  if (spec_unit.empty()) {
    return ParseUnits(metric_unit);
  }
  // both units are specified - they must match.
  auto parsed_metric_units = ParseUnits(metric_unit);
  auto parsed_spec_units   = ParseUnits(spec_unit);
  return parsed_metric_units == parsed_spec_units ? std::optional{parsed_metric_units} : std::nullopt;
}

/**
 * @brief Helper to extend the metric_declarations collection if the given scmi spec block matches the
 *        given metric_declaration.
 * @param metric_declaration The generic metric declaration to match against
 * @param scmi_spec_layout_member The SCMI spec layout member to check for a match
 * @param scmi_spec_layout_member_instance_count The number of times this data event is duplicated in the spec
 *                                               (the containing json block's 'count' field.)
 * @param metric_declarations The collection to extend with new fully-qualified metric declarations if a match is found
 */
static auto AddMetricInstancesIfScmiElementMatches(metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                                   scmi::spec::DataEvent const&                scmi_spec_layout_member,
                                                   uint32_t scmi_spec_layout_member_instance_count,
                                                   std::vector<ScmiMetricDeclaration>& metric_declarations) -> void {
  if (!metric_declaration.collection.register_name.empty() &&
      scmi_spec_layout_member.name != metric_declaration.collection.register_name) {
    return;  // metric's specified register name doesn't match this scmi register, move along.
  }
  // the 'component' field, if given must match, so we can specify which unit (e.g. 'PSS') to look for
  if (metric_declaration.collection.scmi_component_filter.has_value() &&
      scmi_spec_layout_member.component != metric_declaration.collection.scmi_component_filter.value()) {
    return;  // metric's specified component filter doesn't match this scmi register, move along.
  }
  // if the metric declaration and the scmi spec both specify units, they must match
  auto units = GetUnitsIfCompatible(metric_declaration.unit.value_or(""), scmi_spec_layout_member.unit);
  if (units == std::nullopt) {
    return;  // metric's specified unit doesn't match this scmi register, move along.
  }
  if (scmi_spec_layout_member_instance_count > std::numeric_limits<InstanceId>::max()) {
    ASTL_LOG_ERROR("SCMI layout member count {} exceeds maximum supported instances {}",
                   scmi_spec_layout_member_instance_count, std::numeric_limits<InstanceId>::max());
    return;
  }

  // we've found a match, so create entries with the data event id based on the count, and base_de_id
  metric_declarations.reserve(metric_declarations.size() + scmi_spec_layout_member_instance_count);
  for (InstanceId instance_index = 0; instance_index < scmi_spec_layout_member_instance_count; ++instance_index) {
    if (metric_declaration.collection.scmi_instance_filter.has_value()) {
      // check if instance index matches the filter
      std::ostringstream sstream;
      sstream << instance_index;
      if (sstream.str() != metric_declaration.collection.scmi_instance_filter.value()) {
        continue;  // instance index doesn't match filter, move along.
      }
    }
    // compute the data event id for this instance
    ScmiDataEventId de_id = GetDataEventId(scmi_spec_layout_member.base_de_id, instance_index);
    // create the full metric type name, e.g. 'PSS_BMU.0.ENERGY_COUNTER'
    metric_declarations.emplace_back(scmi_spec_layout_member.name, scmi_spec_layout_member.component,
                                     std::to_string(instance_index), units.value(), de_id);
  }
}

/**
 * @brief Get the collection of fully-qualified SCMI register definitions (i.e. PSS_BMU.0.ENERGY_COUNTER)
 * that match the given metric declaration and their corresponding data event ids.
 * @param register_name The register name to look up (i.e. ENERGY_COUNTER)
 * @param scmi_specification The Scmi specification containing the Data Event IDs
 * @return A collection of ScmiMetricDeclaration entries listing all registers matching generic 'register_name'
 *         and details like the specific component+instance name (e.g. PSS_BMU.1.ENERGY_COUNTER) and DE_ID
 */
auto GetMetricRegistersScmiData(metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                ScmiSpecification const& scmi_specification) -> std::vector<ScmiMetricDeclaration> {
  std::vector<ScmiMetricDeclaration> metric_declarations;

  // each layout member consists of a count, which indicates how many times to repeat the members in the 'members' list
  for (const auto& layout_member : scmi_specification.layout) {
    for (const auto& block_member : layout_member.members) {
      AddMetricInstancesIfScmiElementMatches(metric_declaration, block_member, layout_member.count,
                                             metric_declarations);
    }
  }
  return metric_declarations;
}

/**
 * @brief Given a residency metric declaration, find the matching SCMI registers for each state
 *        The results might include a count >1 if the metric declaration doesn't specify an instance filter
 *        All results must come from layout members with the same "component" string.
 */
auto FindMatchingScmiRegistersForResidency(astl::metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                           ScmiSpecification const&                          scmi_spec)
    -> std::expected<ResidencyStateRegisterDefinitions, astl_status_code> {
  ResidencyStateRegisterDefinitions result;

  // if the component is specified, we must match it
  // if we know the count yet, we must match it
  for (const auto& layout_member : scmi_spec.layout) {
    for (const auto& block_member : layout_member.members) {
      // if any metric state's register matches this register, then consider it.
      auto matching_state_name = find_if(metric_declaration.states->begin(), metric_declaration.states->end(),
                                         [&block_member](const auto& state_entry) {
                                           const auto& state_json = state_entry.second;
                                           if (!state_json.contains("register")) {
                                             return false;
                                           }
                                           std::string expected_register_name;
                                           state_json.at("register").get_to(expected_register_name);
                                           return expected_register_name == block_member.name;
                                         });
      if (matching_state_name == metric_declaration.states->end()) {
        continue;  // no states match this register, move along.
      }
      // metric's specified component filter doesn't match this scmi register, move along.
      if (metric_declaration.collection.scmi_component_filter.has_value() &&
          block_member.component != metric_declaration.collection.scmi_component_filter.value()) {
        continue;
      }
      // check that the 'count' field for all registers in the residency metric declaration match.
      if (result.count == 0) {
        result.count = layout_member.count;
      } else if (result.count != layout_member.count) {
        ASTL_LOG_WARNING("Mismatched counts {} and {} for residency metric, checking for other possible matches",
                         result.count, layout_member.count);
        continue;
      }
      // instance count, register name, component name all match, let's add this to the state table.
      result.state_to_base_data_event_id.emplace(matching_state_name->first, block_member.base_de_id);
    }
  }
  // double-check that all the necessary states were found
  for (const auto& [state_name, state_config] : metric_declaration.states.value()) {
    if (result.state_to_base_data_event_id.find(state_name) == result.state_to_base_data_event_id.end()) {
      ASTL_LOG_ERROR("State '{}' in residency metric not found in scmi spec!", state_name);
    }
  }
  if (result.state_to_base_data_event_id.size() != metric_declaration.states->size()) {
    ASTL_LOG_ERROR("Some residency metric states were not found in scmi spec!");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return result;
}

}  // namespace astl::scmi::spec
