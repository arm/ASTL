// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "astl_cxx_compat.hpp"
#include "astl_logger.hpp"
#include "procfs/procfs_metric_builder_parsing_helpers.hpp"

namespace astl::procfs_metric_builder_helpers::detail {

namespace {

constexpr std::string_view kProcfsMetricPlaceholder = "{label}";

auto TryExtractExpandedLabel(std::string_view line, const std::regex& label_pattern,
                             const metrics::spec::ProcfsMetricJsonExpansionSettings& expansion_settings)
    -> astl::compat::optional<std::string> {
  const auto trimmed = procfs::Trim(line);
  if (trimmed.empty()) {
    return astl::compat::nullopt;
  }

  const auto tokens = procfs::SplitWhitespace(trimmed);
  if (expansion_settings.label_token_index >= tokens.size()) {
    return astl::compat::nullopt;
  }
  if (!std::regex_match(tokens[expansion_settings.label_token_index], label_pattern)) {
    return astl::compat::nullopt;
  }
  return tokens[expansion_settings.label_token_index];
}

auto ReadHeaderLabels(std::string_view contents, const std::regex& label_pattern) -> std::vector<std::string> {
  std::vector<std::string> labels;
  const auto               header = procfs::SplitWhitespace(contents.substr(0, contents.find('\n')));
  for (const auto& label : header) {
    if (std::regex_match(label, label_pattern)) {
      labels.push_back(label);
    }
  }
  return labels;
}

auto ReadLineLabels(const std::string& contents, const std::regex& label_pattern,
                    const metrics::spec::ProcfsMetricJsonExpansionSettings& settings) -> std::vector<std::string> {
  std::vector<std::string> labels;
  std::istringstream       stream{contents};
  std::string              line;
  while (std::getline(stream, line)) {
    std::string_view label_line{line};
    if (settings.relative_path == "net/dev") {
      const auto colon = label_line.find(':');
      if (colon == std::string_view::npos) {
        continue;
      }
      label_line = procfs::Trim(label_line.substr(0, colon));
    }
    if (auto label = TryExtractExpandedLabel(label_line, label_pattern, settings); label) {
      labels.push_back(std::move(*label));
    }
  }
  return labels;
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
  constexpr std::string_view cpu_placeholder = "{cpu_index}";
  if (const auto position = metric_name_template.find(cpu_placeholder); position != std::string_view::npos) {
    // Keep the native cpu/cpuN label for reads, but expose a portable metric name.
    auto index = label;
    if (label == "cpu") {
      index = "total";
    } else if ((label.starts_with("cpu") || label.starts_with("CPU"))) {
      index = label.substr(3);
    }
    std::string rendered{metric_name_template};
    rendered.replace(position, cpu_placeholder.size(), index);
    return rendered;
  }
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

auto RenderTemplateValue(const astl::compat::optional<std::string>& value_template, std::string_view label)
    -> astl::compat::optional<std::string> {
  if (!value_template.has_value()) {
    return astl::compat::nullopt;
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

  if (expansion_settings.relative_path == "interrupts" || expansion_settings.relative_path == "softirqs") {
    return ReadHeaderLabels(contents, label_pattern);
  }
  return ReadLineLabels(contents, label_pattern, expansion_settings);
}

}  // namespace astl::procfs_metric_builder_helpers::detail
