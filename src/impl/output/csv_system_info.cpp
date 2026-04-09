/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include "csv_system_info.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "astl/astl_version.h"
#include "common/system_info.hpp"

namespace astl {
namespace {

auto FormatLocalTimestamp(const std::chrono::system_clock::time_point time_point) -> std::string {
  const auto time_value = std::chrono::system_clock::to_time_t(time_point);
  std::tm    local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &time_value);
#else
  localtime_r(&time_value, &local_tm);
#endif

  std::ostringstream stream;
  stream << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return stream.str();
}

}  // namespace

auto EscapeCsvField(std::string_view value) -> std::string {
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

auto FormatReportFloatingValue(double value) -> std::string {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3) << value;
  auto formatted = stream.str();
  while (!formatted.empty() && formatted.back() == '0') {
    formatted.pop_back();
  }
  if (!formatted.empty() && formatted.back() == '.') {
    formatted.pop_back();
  }
  return formatted.empty() ? std::string{"0"} : formatted;
}

auto FormatReportValue(const AstlValue& value) -> std::string {
  return std::visit(
      [](const auto& inner_value) -> std::string {
        using T = std::decay_t<decltype(inner_value)>;
        if constexpr (std::is_same_v<T, bool>) {
          return inner_value ? "1" : "0";
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
          return FormatReportFloatingValue(static_cast<double>(inner_value));
        } else {
          return std::to_string(inner_value);
        }
      },
      value.value);
}

void WriteCollectionInfoCsvSection(std::ostream& output_stream) {
  const auto& info = GetActivePlatformInfo();

  const auto write_field = [&output_stream](const char* field_name, const std::string& field_value) {
    output_stream << field_name << "," << EscapeCsvField(field_value.empty() ? std::string{"<unknown>"} : field_value)
                  << '\n';
  };

  output_stream << "ASTL Build Version," << EscapeCsvField(astlVersionString()) << '\n';
  output_stream << "Collection Date/Time," << EscapeCsvField(FormatLocalTimestamp(std::chrono::system_clock::now()))
                << '\n';
  output_stream << "Command Line," << EscapeCsvField("<not captured>") << '\n';

  write_field("Platform SoC", info.soc_name);
  write_field("Platform Vendor", info.vendor_id);
  write_field("Platform OS", info.os_name);
  write_field("Platform Kernel", info.kernel_release);
  write_field("Platform Host", info.hostname);
  write_field("Platform Architecture", info.architecture);

  output_stream << '\n';
}

}  // namespace astl
