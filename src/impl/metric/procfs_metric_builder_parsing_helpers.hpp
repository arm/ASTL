// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_METRIC_BUILDER_PARSING_HELPERS_HPP_
#define PROCFS_METRIC_BUILDER_PARSING_HELPERS_HPP_

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "astl_file_interface.hpp"
#include "common/procfs_utils.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/procfs_metric_json_declaration.hpp"

namespace astl::procfs_metric_builder_helpers::detail {

auto ReplaceLabelPlaceholder(std::string_view text_template, std::string_view label) -> std::string;

auto RenderExpandedMetricName(std::string_view metric_name_template, std::string_view label) -> std::string;

auto MakeProcfsMetricIdSuffix(std::string_view metric_name) -> std::string;

auto ParseProcfsInputValueType(const metrics::spec::ProcfsMetricJsonCollectionSettings& collection_settings)
    -> std::expected<astl_value_type_t, astl_status_code>;

auto ParseProcfsOutputValueType(astl_value_type_t                           input_value_type,
                                const metrics::spec::MetricJsonDeclaration& metric_declaration, bool is_composite)
    -> std::expected<astl_value_type_t, astl_status_code>;

auto RenderTemplateValue(const std::optional<std::string>& value_template, std::string_view label)
    -> std::optional<std::string>;

auto ParseProcfsFieldDescriptor(const metrics::spec::ProcfsMetricJsonCollectionSettings& collection_settings,
                                const std::optional<std::string>& line_prefix_override = std::nullopt)
    -> std::expected<procfs::FieldDescriptor, astl_status_code>;

auto ParseProcfsFieldDescriptor(const metrics::spec::ProcfsMetricJsonInputSettings& input_settings,
                                const std::optional<std::string>&                   line_prefix_override = std::nullopt)
    -> std::expected<procfs::FieldDescriptor, astl_status_code>;

auto ExpandProcfsLabels(const FileInterface&                                    file_interface,
                        const metrics::spec::ProcfsMetricJsonExpansionSettings& expansion_settings)
    -> std::vector<std::string>;

}  // namespace astl::procfs_metric_builder_helpers::detail

#endif  // PROCFS_METRIC_BUILDER_PARSING_HELPERS_HPP_
