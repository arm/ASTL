// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/scmi_platform_telemetry_spec.hpp"

#include <expected>
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
 * If both are specified and differ, an explicit metric formula allows the declaration to override
 * the output units while still matching the SCMI source register.
 */
inline auto GetUnitsIfCompatible(metrics::spec::MetricJsonDeclaration const& metric_declaration,
                                 std::string_view                            spec_unit) -> std::optional<astl_units_t> {
  const auto metric_unit = metric_declaration.unit.value_or("");
  // if either unit is empty, we consider it a match (no restriction), so use the other.
  if (metric_unit.empty()) {
    return ParseUnits(spec_unit);
  }
  if (spec_unit.empty()) {
    return ParseUnits(metric_unit);
  }
  // both units are specified - they must match unless an explicit declaration formula
  // declares that the metric output unit intentionally differs from the SCMI source unit.
  auto parsed_metric_units = ParseUnits(metric_unit);
  auto parsed_spec_units   = ParseUnits(spec_unit);
  if (parsed_metric_units == parsed_spec_units) {
    return parsed_metric_units;
  }
  if (metric_declaration.formula.has_value()) {
    return parsed_metric_units;
  }
  return std::nullopt;
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
                                                   std::unordered_map<std::string, std::string> const& aliases,
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
  auto units = GetUnitsIfCompatible(metric_declaration, scmi_spec_layout_member.unit);
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
    // check to see if there is a more descriptive name for this component+instance in the aliases map,
    // and use that if so. e.g. "VOLTAGE_RAIL.0" -> "VCPU_C0"
    std::string component_string = scmi_spec_layout_member.component;
    std::string instance_string  = std::to_string(instance_index);
    std::string alias_key        = std::format("{}.{}", component_string, instance_string);
    if (auto iter = aliases.find(alias_key); iter != aliases.end()) {
      component_string = iter->second;
      // if we have a more descriptive alias that includes the instance, we can clear the instance string to avoid
      // redundancy. e.g. "VOLTAGE_RAIL.0" -> "VCPU_C0", no need to have instance string "0" in this case.
      instance_string = "";
    }

    // const auto [descriptive_name, descriptive_instance] = aliases.
    // create the full metric type name, e.g. 'PSS_BMU.0.ENERGY_COUNTER'
    metric_declarations.emplace_back(scmi_spec_layout_member.name, component_string, instance_string, units.value(),
                                     scmi_spec_layout_member.base10_unit_modifier, de_id);
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

  // each member consists of a count, which indicates how many times to repeat the metrics in the 'metrics' list
  for (const auto& layout_member : scmi_specification.members) {
    for (const auto& block_member : layout_member.metrics) {
      AddMetricInstancesIfScmiElementMatches(metric_declaration, block_member.second, layout_member.count,
                                             scmi_specification.aliases, metric_declarations);
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
  for (const auto& layout_member : scmi_spec.members) {
    for (const auto& [member_name, block_member] : layout_member.metrics) {
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
      // Residency scaling for non-zero base10 modifiers is not implemented yet.
      // Fail fast to avoid silently producing incorrect residency values.
      if (block_member.base10_unit_modifier != 0) {
        ASTL_LOG_ERROR(
            "SCMI residency register {} has non-zero base10 unit modifier {}, which is currently not supported",
            block_member.name, block_member.base10_unit_modifier);
        return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
      }
      // instance count, register name, component name all match, let's add this to the state table.
      result.state_to_register_def.emplace(
          // Keep state-level metadata together so residency config/build can stay protocol-agnostic.
          matching_state_name->first, ResidencyStateRegisterDefinitions::StateRegisterDefinition{
                                          .base_data_event_id   = block_member.base_de_id,
                                          .base10_unit_modifier = block_member.base10_unit_modifier});
    }
  }
  // double-check that all the necessary states were found
  for (const auto& [state_name, state_config] : metric_declaration.states.value()) {
    if (!result.state_to_register_def.contains(state_name)) {
      ASTL_LOG_ERROR("State '{}' in residency metric not found in scmi spec!", state_name);
    }
  }
  if (result.state_to_register_def.size() != metric_declaration.states->size()) {
    ASTL_LOG_ERROR("Some residency metric states were not found in scmi spec!");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return result;
}

}  // namespace astl::scmi::spec
