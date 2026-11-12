// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_METRIC_JSON_DECLARATION_HPP_
#define PROCFS_METRIC_JSON_DECLARATION_HPP_

#include <filesystem>
#include <string>
#include <vector>

#include "astl/astl_errors.h"
#include "astl_cxx_compat.hpp"
#include "astl_json.hpp"
#include "config/metric_json_declaration.hpp"

namespace astl::metrics::spec {

struct ProcfsMetricJsonExpansionSettings {
  std::filesystem::path relative_path;
  std::string           match_pattern;
  size_t                label_token_index{0};
};

struct ProcfsMetricJsonInputSettings {
  std::string                         name;
  std::filesystem::path               relative_path;
  std::string                         field_type;
  astl::compat::optional<std::string> field_name;
  astl::compat::optional<std::string> line_prefix;
  astl::compat::optional<size_t>      token_index;
  astl::compat::optional<size_t>      token_start_index;
  astl::compat::optional<size_t>      token_end_index;
  astl::compat::optional<char>        delimiter;
  astl::compat::optional<std::string> split_part;
  astl::compat::optional<std::string> raw_value_type;
};

struct ProcfsMetricJsonCollectionSettings {
  std::filesystem::path                                     relative_path;
  std::string                                               field_type;
  astl::compat::optional<std::string>                       field_name;
  astl::compat::optional<std::string>                       line_prefix;
  astl::compat::optional<std::string>                       raw_value_type;
  astl::compat::optional<size_t>                            token_index;
  astl::compat::optional<ProcfsMetricJsonExpansionSettings> expansion;
  std::vector<ProcfsMetricJsonInputSettings>                inputs;
  bool                                                      requires_previous{false};
};

[[nodiscard]] auto ParseProcfsMetricJsonCollectionSettings(const MetricJsonCollectionSettings& collection_setting)
    -> astl::compat::expected<ProcfsMetricJsonCollectionSettings, astl_status_code>;

}  // namespace astl::metrics::spec

#endif  // PROCFS_METRIC_JSON_DECLARATION_HPP_
