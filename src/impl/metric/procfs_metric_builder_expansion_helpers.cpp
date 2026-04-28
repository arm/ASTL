// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "astl_logger.hpp"
#include "metric/procfs_metric_builder_parsing_helpers.hpp"

namespace astl::procfs_metric_builder_helpers::detail {

namespace {

constexpr std::string_view kProcfsMetricPlaceholder = "{label}";

auto TryExtractExpandedLabel(std::string_view line, const std::regex& label_pattern,
                             const metrics::spec::ProcfsMetricJsonExpansionSettings& expansion_settings)
    -> std::optional<std::string> {
  const auto trimmed = procfs::Trim(line);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  const auto tokens = procfs::SplitWhitespace(trimmed);
  if (expansion_settings.label_token_index >= tokens.size()) {
    return std::nullopt;
  }
  if (!std::regex_match(tokens[expansion_settings.label_token_index], label_pattern)) {
    return std::nullopt;
  }
  return tokens[expansion_settings.label_token_index];
}

}  // namespace

auto ReplaceLabelPlaceholder(std::string_view text_template, std::string_view label) -> std::string {
  std::string rendered{text_template};
  size_t      position = 0;
  while ((position = rendered.find(kProcfsMetricPlaceholder, position)) != std::string::npos) {
    rendered.replace(position, kProcfsMetricPlaceholder.size(), label);
    position += label.size();
  }
  return rendered;
}

auto RenderExpandedMetricName(std::string_view metric_name_template, std::string_view label) -> std::string {
  if (metric_name_template.find(kProcfsMetricPlaceholder) == std::string_view::npos) {
    return std::string{metric_name_template} + "." + std::string{label};
  }
  return ReplaceLabelPlaceholder(metric_name_template, label);
}

auto MakeProcfsMetricIdSuffix(std::string_view metric_name) -> std::string {
  std::string metric_id_suffix;
  metric_id_suffix.reserve(metric_name.size() * 2);
  for (const char character : metric_name) {
    if (character == '.') {
      metric_id_suffix += "::";
      continue;
    }
    metric_id_suffix.push_back(character);
  }
  return metric_id_suffix;
}

auto RenderTemplateValue(const std::optional<std::string>& value_template, std::string_view label)
    -> std::optional<std::string> {
  if (!value_template.has_value()) {
    return std::nullopt;
  }
  return ReplaceLabelPlaceholder(*value_template, label);
}

auto ExpandProcfsLabels(const FileInterface&                                    file_interface,
                        const metrics::spec::ProcfsMetricJsonExpansionSettings& expansion_settings)
    -> std::vector<std::string> {
  std::string contents;
  const auto  status = file_interface.Read(expansion_settings.relative_path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_WARNING("Unable to read PROCFS source {} while expanding procfs metrics",
                     expansion_settings.relative_path.string());
    return {};
  }

  std::regex label_pattern;
  try {
    label_pattern = std::regex{expansion_settings.match_pattern};
  } catch (const std::regex_error& e) {
    ASTL_LOG_ERROR("Invalid regex pattern '{}': {}", expansion_settings.match_pattern, e.what());
    return {};
  }

  std::vector<std::string> labels;
  size_t                   position = 0;
  while (position <= contents.size()) {
    const size_t end = contents.find('\n', position);
    const auto   line =
        std::string_view{contents}.substr(position, end == std::string::npos ? std::string_view::npos : end - position);
    if (auto label = TryExtractExpandedLabel(line, label_pattern, expansion_settings); label.has_value()) {
      labels.push_back(std::move(*label));
    }
    if (end == std::string::npos) {
      break;
    }
    position = end + 1;
  }
  return labels;
}

}  // namespace astl::procfs_metric_builder_helpers::detail
