/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include "csv_system_info.hpp"

#include <string>
#include <string_view>

#include "common/system_info.hpp"

namespace astl {
namespace {

auto EscapeCsvField(std::string_view value) -> std::string {
  const bool needs_quotes = value.find_first_of(",\"\n\r") != std::string_view::npos;
  if (!needs_quotes) {
    return std::string(value);
  }

  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const char character : value) {
    if (character == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

}  // namespace

void WriteSystemInfoCsvSection(std::ostream& output_stream) {
  const auto& info = GetActivePlatformInfo();

  output_stream << "System Info\n";
  output_stream << "Field,Value\n";

  const auto write_field = [&output_stream](const char* field_name, const std::string& field_value) {
    output_stream << field_name << "," << EscapeCsvField(field_value.empty() ? std::string{"<unknown>"} : field_value)
                  << '\n';
  };

  write_field("SoC", info.soc_name);
  write_field("Vendor ID", info.vendor_id);
  write_field("OS", info.os_name);
  write_field("Kernel", info.kernel_name);
  write_field("Kernel release", info.kernel_release);
  write_field("Kernel version", info.kernel_version);
  write_field("Firmware", info.firmware_version);
  write_field("Host", info.hostname);
  write_field("Architecture", info.architecture);

  output_stream << '\n';
}

}  // namespace astl
