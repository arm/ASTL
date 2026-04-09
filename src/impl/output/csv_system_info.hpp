/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#ifndef CSV_SYSTEM_INFO_HPP_
#define CSV_SYSTEM_INFO_HPP_

#include <iosfwd>
#include <string>
#include <string_view>

#include "common/astl_value.hpp"

namespace astl {

auto EscapeCsvField(std::string_view value) -> std::string;
auto FormatReportFloatingValue(double value) -> std::string;
auto FormatReportValue(const AstlValue& value) -> std::string;
void WriteCollectionInfoCsvSection(std::ostream& output_stream);

}  // namespace astl

#endif  // CSV_SYSTEM_INFO_HPP_
