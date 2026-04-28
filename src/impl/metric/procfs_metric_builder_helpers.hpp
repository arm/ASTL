// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_METRIC_BUILDER_HELPERS_HPP_
#define PROCFS_METRIC_BUILDER_HELPERS_HPP_

#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "astl_file_interface.hpp"
#include "common/metric_config.hpp"
#include "config/astl_configuration.hpp"
#include "config/metric_json_declaration.hpp"

namespace astl {

struct ITarget;

namespace procfs_metric_builder_helpers {

auto LoadProcfsMetricDeclarations(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::pair<std::string, metrics::spec::MetricJsonDeclaration>>, astl_status_code>;

auto CreateProcfsMetricConfigs(const std::string&                          metric_name_template,
                               const metrics::spec::MetricJsonDeclaration& metric_declaration, const ITarget* target,
                               const FileInterface& file_interface)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code>;

}  // namespace procfs_metric_builder_helpers

}  // namespace astl

#endif  // PROCFS_METRIC_BUILDER_HELPERS_HPP_
