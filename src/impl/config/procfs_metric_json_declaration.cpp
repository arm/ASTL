// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/procfs_metric_json_declaration.hpp"

#include "astl_internal_status.hpp"
#include "astl_logger.hpp"
#include "astl_utils.hpp"

namespace astl::metrics::spec {

namespace {

auto ParsePath(const nlohmann::json& json_value, const char* field_name)
    -> std::expected<std::filesystem::path, astl_status_code> {
  if (!json_value.contains(field_name)) {
    ASTL_LOG_ERROR("PROCFS metric declaration missing collection.{}", field_name);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return json_value.at(field_name).get<std::string>();
}

auto ParseInputSettings(const nlohmann::json& json_value)
    -> std::expected<ProcfsMetricJsonInputSettings, astl_status_code> {
  if (!json_value.is_object()) {
    ASTL_LOG_ERROR("PROCFS composite input declaration must be an object");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ProcfsMetricJsonInputSettings input;
  if (!json_value.contains("name")) {
    ASTL_LOG_ERROR("PROCFS composite input declaration missing name");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  json_value.at("name").get_to(input.name);

  auto relative_path = ParsePath(json_value, "relative_path");
  if (!relative_path.has_value()) {
    return std::unexpected(relative_path.error());
  }
  input.relative_path = std::move(*relative_path);

  if (!json_value.contains("field_type")) {
    ASTL_LOG_ERROR("PROCFS composite input declaration missing field_type");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  json_value.at("field_type").get_to(input.field_type);

  if (json_value.contains("field_name")) {
    json_value.at("field_name").get_to(input.field_name);
  }
  if (json_value.contains("line_prefix")) {
    json_value.at("line_prefix").get_to(input.line_prefix);
  }
  if (json_value.contains("token_index")) {
    json_value.at("token_index").get_to(input.token_index);
  }
  if (json_value.contains("token_start_index")) {
    json_value.at("token_start_index").get_to(input.token_start_index);
  }
  if (json_value.contains("token_end_index")) {
    json_value.at("token_end_index").get_to(input.token_end_index);
  }
  if (json_value.contains("delimiter")) {
    const auto delimiter = json_value.at("delimiter").get<std::string>();
    if (delimiter.size() != 1) {
      ASTL_LOG_ERROR("PROCFS composite input delimiter must be a single character");
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    input.delimiter = delimiter.front();
  }
  if (json_value.contains("split_part")) {
    json_value.at("split_part").get_to(input.split_part);
  }
  if (json_value.contains("raw_value_type")) {
    json_value.at("raw_value_type").get_to(input.raw_value_type);
  }

  return input;
}

auto ParseExpansionSettings(const nlohmann::json& json_value)
    -> std::expected<ProcfsMetricJsonExpansionSettings, astl_status_code> {
  if (json_value.is_string()) {
    const auto expand_mode = ToLowerCopy(json_value.get<std::string>());
    if (expand_mode != "cpu_lines") {
      ASTL_LOG_ERROR("Unsupported PROCFS expand mode '{}'", expand_mode);
      return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
    }
    return ProcfsMetricJsonExpansionSettings{
        .relative_path     = "stat",
        .match_pattern     = "^cpu[0-9]*$",
        .label_token_index = 0,
    };
  }

  if (!json_value.is_object()) {
    ASTL_LOG_ERROR("PROCFS expansion settings must be an object or string");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ProcfsMetricJsonExpansionSettings expansion;
  auto                              relative_path = ParsePath(json_value, "relative_path");
  if (!relative_path.has_value()) {
    return std::unexpected(relative_path.error());
  }
  expansion.relative_path = std::move(*relative_path);

  if (!json_value.contains("match_pattern")) {
    ASTL_LOG_ERROR("PROCFS expansion settings missing match_pattern");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  json_value.at("match_pattern").get_to(expansion.match_pattern);
  if (json_value.contains("label_token_index")) {
    json_value.at("label_token_index").get_to(expansion.label_token_index);
  }
  return expansion;
}

auto HasCompositeInputs(const nlohmann::json& json_value) -> bool { return json_value.contains("inputs"); }

auto ParseCollectionRelativePath(ProcfsMetricJsonCollectionSettings& settings, const nlohmann::json& json_value)
    -> std::expected<void, astl_status_code> {
  if (json_value.contains("relative_path")) {
    settings.relative_path = json_value.at("relative_path").get<std::string>();
    return {};
  }
  if (json_value.contains("path")) {
    settings.relative_path = json_value.at("path").get<std::string>();
    return {};
  }
  if (!HasCompositeInputs(json_value)) {
    ASTL_LOG_ERROR("PROCFS metric declaration missing collection.relative_path");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return {};
}

auto ParseCollectionFieldType(ProcfsMetricJsonCollectionSettings& settings, const nlohmann::json& json_value)
    -> std::expected<void, astl_status_code> {
  if (json_value.contains("field_type")) {
    json_value.at("field_type").get_to(settings.field_type);
    return {};
  }
  if (!HasCompositeInputs(json_value)) {
    ASTL_LOG_ERROR("PROCFS metric declaration missing collection.field_type");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return {};
}

auto ParseOptionalCollectionFields(ProcfsMetricJsonCollectionSettings& settings, const nlohmann::json& json_value)
    -> void {
  if (json_value.contains("field_name")) {
    json_value.at("field_name").get_to(settings.field_name);
  }
  if (json_value.contains("line_prefix")) {
    json_value.at("line_prefix").get_to(settings.line_prefix);
  }
  if (json_value.contains("raw_value_type")) {
    json_value.at("raw_value_type").get_to(settings.raw_value_type);
  }
  if (json_value.contains("requires_previous")) {
    json_value.at("requires_previous").get_to(settings.requires_previous);
  }
}

auto RejectUnsupportedLegacyCollectionFields(const nlohmann::json& json_value)
    -> std::expected<void, astl_status_code> {
  if (json_value.contains("composite_formula")) {
    ASTL_LOG_ERROR("PROCFS collection.composite_formula is no longer supported; use top-level formula");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return {};
}

auto ParseCollectionExpansion(ProcfsMetricJsonCollectionSettings& settings, const nlohmann::json& json_value)
    -> std::expected<void, astl_status_code> {
  if (!json_value.contains("expand")) {
    return {};
  }

  auto expansion = ParseExpansionSettings(json_value.at("expand"));
  if (!expansion.has_value()) {
    return std::unexpected(expansion.error());
  }
  if (expansion->relative_path.empty()) {
    expansion->relative_path = settings.relative_path;
  }
  settings.expansion = std::move(*expansion);
  return {};
}

auto ParseCollectionInputs(ProcfsMetricJsonCollectionSettings& settings, const nlohmann::json& json_value)
    -> std::expected<void, astl_status_code> {
  if (!HasCompositeInputs(json_value)) {
    return {};
  }

  const auto& inputs_json = json_value.at("inputs");
  if (!inputs_json.is_array() || inputs_json.empty()) {
    ASTL_LOG_ERROR("PROCFS composite inputs must be a non-empty array");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  settings.inputs.reserve(inputs_json.size());
  for (const auto& input_json : inputs_json) {
    auto input = ParseInputSettings(input_json);
    if (!input.has_value()) {
      return std::unexpected(input.error());
    }
    settings.inputs.push_back(std::move(*input));
  }
  return {};
}

}  // namespace

auto ParseProcfsMetricJsonCollectionSettings(const MetricJsonCollectionSettings& collection_setting)
    -> std::expected<ProcfsMetricJsonCollectionSettings, astl_status_code> {
  if (ToLowerCopy(collection_setting.protocol) != "procfs") {
    ASTL_LOG_ERROR("PROCFS collection parser received unsupported protocol '{}'", collection_setting.protocol);
    return std::unexpected(astl::kInternalNotImplemented);
  }
  if (!collection_setting.raw_json.is_object()) {
    ASTL_LOG_ERROR("PROCFS collection settings must be a JSON object");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  ProcfsMetricJsonCollectionSettings settings;
  auto                               relative_path = ParseCollectionRelativePath(settings, collection_setting.raw_json);
  if (!relative_path.has_value()) {
    return std::unexpected(relative_path.error());
  }

  auto field_type = ParseCollectionFieldType(settings, collection_setting.raw_json);
  if (!field_type.has_value()) {
    return std::unexpected(field_type.error());
  }

  auto unsupported_fields = RejectUnsupportedLegacyCollectionFields(collection_setting.raw_json);
  if (!unsupported_fields.has_value()) {
    return std::unexpected(unsupported_fields.error());
  }

  ParseOptionalCollectionFields(settings, collection_setting.raw_json);

  auto expansion = ParseCollectionExpansion(settings, collection_setting.raw_json);
  if (!expansion.has_value()) {
    return std::unexpected(expansion.error());
  }

  auto inputs = ParseCollectionInputs(settings, collection_setting.raw_json);
  if (!inputs.has_value()) {
    return std::unexpected(inputs.error());
  }

  return settings;
}

}  // namespace astl::metrics::spec
