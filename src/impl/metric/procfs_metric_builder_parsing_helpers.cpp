// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "metric/procfs_metric_builder_parsing_helpers.hpp"

#include <optional>
#include <string>

#include "astl_logger.hpp"
#include "astl_utils.hpp"

namespace astl::procfs_metric_builder_helpers::detail {

namespace {

struct ProcfsFieldDescriptorDeclaration {
  std::filesystem::path      relative_path;
  std::string                field_type;
  std::optional<std::string> field_name;
  std::optional<std::string> line_prefix;
  std::optional<size_t>      token_index;
  std::optional<size_t>      token_start_index;
  std::optional<size_t>      token_end_index;
  std::optional<char>        delimiter;
  std::optional<std::string> split_part;
  std::optional<std::string> raw_value_type;
};

struct SplitTokenFieldDescriptorParams {
  std::filesystem::path      relative_path;
  std::string                line_prefix;
  std::optional<size_t>      token_index;
  std::optional<char>        delimiter;
  std::optional<std::string> split_part;
  std::optional<std::string> raw_value_type;
};

auto ParseConfiguredValueType(const std::optional<std::string>& value_type_text) -> std::optional<astl_value_type_t> {
  if (!value_type_text.has_value()) {
    return std::nullopt;
  }

  const auto                       lowered = ToLowerCopy(*value_type_text);
  std::optional<astl_value_type_t> parsed_value_type;
  if (lowered == "uint64") {
    parsed_value_type = ASTL_VALUE_UINT64;
  } else if (lowered == "uint32") {
    parsed_value_type = ASTL_VALUE_UINT32;
  } else if (lowered == "float64" || lowered == "double") {
    parsed_value_type = ASTL_VALUE_FLOAT64;
  } else if (lowered == "float32" || lowered == "float") {
    parsed_value_type = ASTL_VALUE_FLOAT32;
  }
  return parsed_value_type;
}

auto ParseProcfsRawValueType(const std::optional<std::string>& raw_value_type_text,
                             astl_value_type_t                 default_value_type)
    -> std::expected<astl_value_type_t, astl_status_code> {
  if (!raw_value_type_text.has_value()) {
    return default_value_type;
  }
  const auto parsed_value_type = ParseConfiguredValueType(raw_value_type_text);
  if (!parsed_value_type.has_value()) {
    ASTL_LOG_ERROR("Unsupported PROCFS raw_value_type '{}'", *raw_value_type_text);
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return *parsed_value_type;
}

auto ParseSplitTokenPart(const std::optional<std::string>& split_part)
    -> std::expected<procfs::SplitTokenPart, astl_status_code> {
  const auto lowered = ToLowerCopy(split_part.value_or("before_delimiter"));
  if (lowered == "before_delimiter" || lowered == "before") {
    return procfs::SplitTokenPart::BEFORE_DELIMITER;
  }
  if (lowered == "after_delimiter" || lowered == "after") {
    return procfs::SplitTokenPart::AFTER_DELIMITER;
  }

  ASTL_LOG_ERROR("Unsupported PROCFS split_part '{}'", lowered);
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

auto ParseKeyValueFieldDescriptor(const std::filesystem::path&      relative_path,
                                  const std::optional<std::string>& field_name,
                                  const std::optional<std::string>& raw_value_type)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  if (!field_name.has_value()) {
    ASTL_LOG_ERROR("PROCFS key_value declaration missing field_name");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto parsed_raw_type = ParseProcfsRawValueType(raw_value_type, ASTL_VALUE_UINT64);
  if (!parsed_raw_type.has_value()) {
    return std::unexpected(parsed_raw_type.error());
  }
  return procfs::KeyValueField{relative_path, *field_name, *parsed_raw_type};
}

auto ParseTokenFieldDescriptor(const std::filesystem::path& relative_path, const std::string& line_prefix,
                               const std::optional<size_t>&      token_index,
                               const std::optional<std::string>& raw_value_type)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  if (!token_index.has_value()) {
    ASTL_LOG_ERROR("PROCFS token declaration missing token_index");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto parsed_raw_type = ParseProcfsRawValueType(raw_value_type, ASTL_VALUE_UINT64);
  if (!parsed_raw_type.has_value()) {
    return std::unexpected(parsed_raw_type.error());
  }
  return procfs::TokenField{relative_path, line_prefix, *token_index, *parsed_raw_type};
}

auto ParseSplitTokenFieldDescriptor(const SplitTokenFieldDescriptorParams& params)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  if (!params.token_index.has_value()) {
    ASTL_LOG_ERROR("PROCFS split_token declaration missing token_index");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  auto parsed_raw_type = ParseProcfsRawValueType(params.raw_value_type, ASTL_VALUE_UINT64);
  if (!parsed_raw_type.has_value()) {
    return std::unexpected(parsed_raw_type.error());
  }
  auto parsed_part = ParseSplitTokenPart(params.split_part);
  if (!parsed_part.has_value()) {
    return std::unexpected(parsed_part.error());
  }
  return procfs::SplitTokenField{params.relative_path,           params.line_prefix, *params.token_index,
                                 params.delimiter.value_or('/'), *parsed_part,       *parsed_raw_type};
}

auto ParseTokenSumFieldDescriptor(const std::filesystem::path& relative_path, const std::string& line_prefix,
                                  const std::optional<size_t>& token_start_index,
                                  const std::optional<size_t>& token_end_index)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  if (!token_start_index.has_value() || !token_end_index.has_value()) {
    ASTL_LOG_ERROR("PROCFS token_sum declaration missing token_start_index or token_end_index");
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return procfs::TokenSumField{relative_path, line_prefix, *token_start_index, *token_end_index};
}

}  // namespace

auto ParseProcfsInputValueType(const metrics::spec::ProcfsMetricJsonCollectionSettings& collection_settings)
    -> std::expected<astl_value_type_t, astl_status_code> {
  if (!collection_settings.inputs.empty()) {
    return ASTL_VALUE_UNKNOWN;
  }

  const auto field_type = ToLowerCopy(collection_settings.field_type);
  if (field_type == "key_value" || field_type == "token" || field_type == "split_token") {
    return ParseProcfsRawValueType(collection_settings.raw_value_type, ASTL_VALUE_UINT64);
  }

  std::expected<astl_value_type_t, astl_status_code> parsed_input_value_type =
      std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
  if (field_type == "token_sum" || field_type == "mem_used") {
    parsed_input_value_type = ASTL_VALUE_UINT64;
  } else if (field_type == "mem_used_percent" || field_type == "cpu_utilization") {
    parsed_input_value_type = ASTL_VALUE_FLOAT64;
  } else {
    ASTL_LOG_ERROR("Unsupported PROCFS field_type '{}'", field_type);
  }
  return parsed_input_value_type;
}

auto ParseProcfsOutputValueType(astl_value_type_t                           input_value_type,
                                const metrics::spec::MetricJsonDeclaration& metric_declaration, bool is_composite)
    -> std::expected<astl_value_type_t, astl_status_code> {
  if (metric_declaration.value_type.has_value()) {
    const auto configured_value_type = ParseConfiguredValueType(metric_declaration.value_type);
    if (!configured_value_type.has_value()) {
      ASTL_LOG_ERROR("Unsupported PROCFS metric value_type '{}'", *metric_declaration.value_type);
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return *configured_value_type;
  }

  if (is_composite) {
    if (ToLowerCopy(metric_declaration.unit.value_or("")) == "percent") {
      return ASTL_VALUE_FLOAT64;
    }
    return ASTL_VALUE_UINT64;
  }

  return input_value_type;
}

auto ParseProcfsFieldDescriptor(const ProcfsFieldDescriptorDeclaration& declaration,
                                const std::optional<std::string>&       line_prefix_override)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  const auto lowered_field_type   = ToLowerCopy(declaration.field_type);
  const auto rendered_line_prefix = line_prefix_override.has_value() ? line_prefix_override : declaration.line_prefix;

  std::expected<procfs::FieldDescriptor, astl_status_code> parsed_descriptor =
      std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
  if (lowered_field_type == "key_value") {
    parsed_descriptor =
        ParseKeyValueFieldDescriptor(declaration.relative_path, declaration.field_name, declaration.raw_value_type);
  } else if (lowered_field_type == "token") {
    parsed_descriptor = ParseTokenFieldDescriptor(declaration.relative_path, rendered_line_prefix.value_or(""),
                                                  declaration.token_index, declaration.raw_value_type);
  } else if (lowered_field_type == "split_token") {
    parsed_descriptor = ParseSplitTokenFieldDescriptor(SplitTokenFieldDescriptorParams{
        .relative_path  = declaration.relative_path,
        .line_prefix    = rendered_line_prefix.value_or(""),
        .token_index    = declaration.token_index,
        .delimiter      = declaration.delimiter,
        .split_part     = declaration.split_part,
        .raw_value_type = declaration.raw_value_type,
    });
  } else if (lowered_field_type == "token_sum") {
    parsed_descriptor = ParseTokenSumFieldDescriptor(declaration.relative_path, rendered_line_prefix.value_or(""),
                                                     declaration.token_start_index, declaration.token_end_index);
  } else if (lowered_field_type == "mem_used") {
    parsed_descriptor = procfs::MemUsedField{declaration.relative_path};
  } else if (lowered_field_type == "mem_used_percent") {
    parsed_descriptor = procfs::MemUsedPercentField{declaration.relative_path};
  } else if (lowered_field_type == "cpu_utilization") {
    parsed_descriptor = procfs::CpuUtilizationField{declaration.relative_path, rendered_line_prefix.value_or("cpu")};
  } else {
    ASTL_LOG_ERROR("Unsupported PROCFS field_type '{}'", declaration.field_type);
  }
  return parsed_descriptor;
}

auto ParseProcfsFieldDescriptor(const metrics::spec::ProcfsMetricJsonCollectionSettings& collection_settings,
                                const std::optional<std::string>&                        line_prefix_override)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  return ParseProcfsFieldDescriptor(
      ProcfsFieldDescriptorDeclaration{
          .relative_path     = collection_settings.relative_path,
          .field_type        = collection_settings.field_type,
          .field_name        = collection_settings.field_name,
          .line_prefix       = collection_settings.line_prefix,
          .token_index       = std::nullopt,
          .token_start_index = std::nullopt,
          .token_end_index   = std::nullopt,
          .delimiter         = std::nullopt,
          .split_part        = std::nullopt,
          .raw_value_type    = collection_settings.raw_value_type,
      },
      line_prefix_override);
}

auto ParseProcfsFieldDescriptor(const metrics::spec::ProcfsMetricJsonInputSettings& input_settings,
                                const std::optional<std::string>&                   line_prefix_override)
    -> std::expected<procfs::FieldDescriptor, astl_status_code> {
  return ParseProcfsFieldDescriptor(
      ProcfsFieldDescriptorDeclaration{
          .relative_path     = input_settings.relative_path,
          .field_type        = input_settings.field_type,
          .field_name        = input_settings.field_name,
          .line_prefix       = input_settings.line_prefix,
          .token_index       = input_settings.token_index,
          .token_start_index = input_settings.token_start_index,
          .token_end_index   = input_settings.token_end_index,
          .delimiter         = input_settings.delimiter,
          .split_part        = input_settings.split_part,
          .raw_value_type    = input_settings.raw_value_type,
      },
      line_prefix_override);
}

}  // namespace astl::procfs_metric_builder_helpers::detail
