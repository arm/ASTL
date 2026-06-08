// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <string_view>

#include "astl/astl.h"
#include "astl_magic_enum.hpp"

auto astlStatusString(astl_status_code status) noexcept -> const char* {
  std::string_view name = magic_enum::enum_name(status);
  // ignore the first part of the name, which is "ASTL_STATUS_";
  constexpr size_t prefix_length = 12;  // length of "ASTL_STATUS_";
  return name.empty() ? "UNKNOWN_ERROR" : name.data() + prefix_length;
}
