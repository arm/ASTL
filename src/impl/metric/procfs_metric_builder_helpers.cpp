// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/procfs_metric_builder_helpers.hpp"

#include <algorithm>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/procfs_metric_json_declaration.hpp"
#include "metric/formula_builder.hpp"
#include "metric/procfs_composite_metricconfig.hpp"
#include "metric/procfs_metric_builder_parsing_helpers.hpp"
#include "operation/procfs_operation_builder.hpp"

namespace astl::procfs_metric_builder_helpers {

namespace {

struct ProcfsMetricBuildSettings {
  metrics::spec::ProcfsMetricJsonCollectionSettings collection_settings;
  astl_metric_type_t                                metric_type{ASTL_METRIC_UNKNOWN};
  astl_value_type_t                                 input_value_type{ASTL_VALUE_UNKNOWN};
  astl_units_t                                      units{ASTL_UNITS_NONE};
  astl_metric_identifier_t                          identifier{ASTL_METRIC_IDENTIFIER_UNKNOWN};
  astl_value_type_t                                 value_type{ASTL_VALUE_UNKNOWN};
  bool                                              is_composite{false};
};

struct ProcfsRenderedMetricMetadata {
  std::string              metric_name;
  std::string              description;
  std::vector<std::string> metric_groups;
  std::string              metric_id;
};

auto BuildProcfsFormula(const metrics::spec::MetricJsonDeclaration& metric_declaration)
    -> std::expected<AnyFormula, astl_status_code> {
  return BuildFormula(metric_declaration.formula);
}

auto IsProcfsMetricDeclaration(const metrics::spec::MetricJsonDeclaration& metric_declaration) -> bool {
  return ToLowerCopy(metric_declaration.collection.protocol) == "procfs";
}

auto ParseProcfsMetricBuildSettings(const std::string&                          metric_name_template,
                                    const metrics::spec::MetricJsonDeclaration& metric_declaration)
    -> std::expected<ProcfsMetricBuildSettings, astl_status_code> {
  ProcfsMetricBuildSettings settings;
  settings.metric_type = ParseMetricType(metric_declaration.metric_type);
  if (settings.metric_type == ASTL_METRIC_UNKNOWN) {
    ASTL_LOG_ERROR("Unsupported PROCFS metric type '{}' for metric {}", metric_declaration.metric_type,
                   metric_name_template);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  auto collection_settings = metrics::spec::ParseProcfsMetricJsonCollectionSettings(metric_declaration.collection);
  if (!collection_settings.has_value()) {
    return std::unexpected(collection_settings.error());
  }
  settings.collection_settings = std::move(*collection_settings);

  auto input_value_type = detail::ParseProcfsInputValueType(settings.collection_settings);
  if (!input_value_type.has_value()) {
    return std::unexpected(input_value_type.error());
  }
  settings.input_value_type = *input_value_type;

  settings.units        = ParseUnits(metric_declaration.unit.value_or(""));
  settings.identifier   = ParseMetricIdentifier(metric_declaration.identifier);
  settings.is_composite = !settings.collection_settings.inputs.empty();

  auto value_type =
      detail::ParseProcfsOutputValueType(settings.input_value_type, metric_declaration, settings.is_composite);
  if (!value_type.has_value()) {
    return std::unexpected(value_type.error());
  }
  settings.value_type = *value_type;
  return settings;
}

auto ResolveProcfsMetricLabels(const FileInterface&                                     file_interface,
                               const metrics::spec::ProcfsMetricJsonCollectionSettings& collection_settings)
    -> std::vector<std::string> {
  if (!collection_settings.expansion.has_value()) {
    return {std::string{}};
  }
  return detail::ExpandProcfsLabels(file_interface, *collection_settings.expansion);
}

auto RenderProcfsMetricMetadata(const std::string&                          metric_name_template,
                                const metrics::spec::MetricJsonDeclaration& metric_declaration, const ITarget* target,
                                const std::string& label) -> ProcfsRenderedMetricMetadata {
  const auto metric_name =
      label.empty() ? metric_name_template : detail::RenderExpandedMetricName(metric_name_template, label);
  const auto description = label.empty() ? metric_declaration.description
                                         : detail::ReplaceLabelPlaceholder(metric_declaration.description, label);

  return ProcfsRenderedMetricMetadata{
      .metric_name   = metric_name,
      .description   = description,
      .metric_groups = metric_declaration.metric_groups.value_or(std::vector<std::string>{}),
      .metric_id     = "procfs::" + target->Name() + "::" + detail::MakeProcfsMetricIdSuffix(metric_name),
  };
}

auto BuildCompositeInputBindings(const metrics::spec::ProcfsMetricJsonCollectionSettings& collection_settings,
                                 const std::string&                                       label)
    -> std::expected<std::vector<ProcfsCompositeMetricConfig::InputBinding>, astl_status_code> {
  std::vector<ProcfsCompositeMetricConfig::InputBinding> inputs;
  inputs.reserve(collection_settings.inputs.size());
  for (const auto& input : collection_settings.inputs) {
    auto field_descriptor = detail::ParseProcfsFieldDescriptor(metrics::spec::ProcfsMetricJsonInputSettings{
        .name              = input.name,
        .relative_path     = input.relative_path,
        .field_type        = input.field_type,
        .field_name        = detail::RenderTemplateValue(input.field_name, label),
        .line_prefix       = detail::RenderTemplateValue(input.line_prefix, label),
        .token_index       = input.token_index,
        .token_start_index = input.token_start_index,
        .token_end_index   = input.token_end_index,
        .delimiter         = input.delimiter,
        .split_part        = input.split_part,
        .raw_value_type    = input.raw_value_type,
    });
    if (!field_descriptor.has_value()) {
      return std::unexpected(field_descriptor.error());
    }
    inputs.push_back(ProcfsCompositeMetricConfig::InputBinding{input.name, std::move(*field_descriptor)});
  }
  return inputs;
}

auto BuildCompositeMetricConfig(const ProcfsRenderedMetricMetadata&         metadata,
                                const metrics::spec::MetricJsonDeclaration& metric_declaration,
                                const ProcfsMetricBuildSettings& build_settings, const std::string& label)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  auto inputs = BuildCompositeInputBindings(build_settings.collection_settings, label);
  if (!inputs.has_value()) {
    return std::unexpected(inputs.error());
  }

  if (!metric_declaration.formula.has_value()) {
    ASTL_LOG_ERROR("PROCFS composite metric {} is missing formula", metadata.metric_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  if (!metric_declaration.formula->is_string()) {
    ASTL_LOG_ERROR("PROCFS composite metric {} formula must be a string tinyexpr expression", metadata.metric_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  auto formula_text = metric_declaration.formula->get<std::string>();
  if (formula_text.empty()) {
    ASTL_LOG_ERROR("PROCFS composite metric {} formula cannot be empty", metadata.metric_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  return std::make_unique<ProcfsCompositeMetricConfig>(ProcfsCompositeMetricConfig::CreateParams{
      .name              = metadata.metric_name,
      .description       = metadata.description,
      .units             = build_settings.units,
      .value_type        = build_settings.value_type,
      .identifier        = build_settings.identifier,
      .metric_type       = build_settings.metric_type,
      .inputs            = std::move(*inputs),
      .requires_previous = build_settings.collection_settings.requires_previous,
      .formula_text      = std::move(formula_text),
      .metric_groups     = metadata.metric_groups,
      .metric_id         = metadata.metric_id,
  });
}

auto BuildStandardProcfsMetricConfig(const ProcfsRenderedMetricMetadata&         metadata,
                                     const metrics::spec::MetricJsonDeclaration& metric_declaration,
                                     const ProcfsMetricBuildSettings& build_settings, const std::string& label)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  const auto line_prefix_override = label.empty() ? std::optional<std::string>{} : std::optional<std::string>{label};
  auto field_descriptor = detail::ParseProcfsFieldDescriptor(build_settings.collection_settings, line_prefix_override);
  if (!field_descriptor.has_value()) {
    return std::unexpected(field_descriptor.error());
  }

  auto formula_result = BuildProcfsFormula(metric_declaration);
  if (!formula_result.has_value()) {
    return std::unexpected(formula_result.error());
  }

  return std::make_unique<MetricConfig>(
      metadata.metric_name, metadata.description, build_settings.units, build_settings.value_type,
      build_settings.identifier, build_settings.metric_type, CollectorType::PROCFS,
      ProcfsOperationBuilder{std::move(*field_descriptor)}, std::move(formula_result.value()),
      build_settings.input_value_type, metadata.metric_groups, metadata.metric_id);
}

}  // namespace

auto LoadProcfsMetricDeclarations(const AstlConfiguration& configuration)
    -> std::expected<std::vector<std::pair<std::string, metrics::spec::MetricJsonDeclaration>>, astl_status_code> {
  const auto procfs_metrics_dir = configuration.metrics_dir_path / "procfs";
  if (!std::filesystem::exists(procfs_metrics_dir)) {
    ASTL_LOG_INFO("PROCFS metrics config directory not found at {}, skipping PROCFS metric declarations",
                  procfs_metrics_dir.string());
    return std::vector<std::pair<std::string, metrics::spec::MetricJsonDeclaration>>{};
  }

  std::vector<std::filesystem::path> declaration_files;
  for (const auto& entry : std::filesystem::directory_iterator{procfs_metrics_dir}) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      declaration_files.push_back(entry.path());
    }
  }
  std::ranges::sort(declaration_files);

  std::vector<std::pair<std::string, metrics::spec::MetricJsonDeclaration>> metric_declarations;
  for (const auto& declaration_file : declaration_files) {
    auto declaration = config::TryParseJsonFile<metrics::spec::MetricsDeclaration>(declaration_file);
    if (!declaration.has_value()) {
      return std::unexpected(declaration.error());
    }
    for (const auto& [metric_name, metric_declaration] : declaration->metrics) {
      if (!IsProcfsMetricDeclaration(metric_declaration)) {
        continue;
      }
      metric_declarations.emplace_back(metric_name, metric_declaration);
    }
  }
  return metric_declarations;
}

auto CreateProcfsMetricConfigs(const std::string&                          metric_name_template,
                               const metrics::spec::MetricJsonDeclaration& metric_declaration, const ITarget* target,
                               const FileInterface& file_interface)
    -> std::expected<std::vector<std::unique_ptr<MetricConfig>>, astl_status_code> {
  auto build_settings = ParseProcfsMetricBuildSettings(metric_name_template, metric_declaration);
  if (!build_settings.has_value()) {
    return std::unexpected(build_settings.error());
  }

  const auto labels = ResolveProcfsMetricLabels(file_interface, build_settings->collection_settings);

  std::vector<std::unique_ptr<MetricConfig>> metric_configs;
  metric_configs.reserve(labels.size());
  for (const auto& label : labels) {
    const auto metadata      = RenderProcfsMetricMetadata(metric_name_template, metric_declaration, target, label);
    auto       metric_config = build_settings->is_composite
                                   ? BuildCompositeMetricConfig(metadata, metric_declaration, *build_settings, label)
                                   : BuildStandardProcfsMetricConfig(metadata, metric_declaration, *build_settings, label);
    if (!metric_config.has_value()) {
      return std::unexpected(metric_config.error());
    }
    metric_configs.push_back(std::move(*metric_config));
  }

  return metric_configs;
}

}  // namespace astl::procfs_metric_builder_helpers
