// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_UTILS_READERS_PRIMITIVES_HPP_
#define PROCFS_UTILS_READERS_PRIMITIVES_HPP_

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "common/procfs_utils.hpp"

namespace astl::procfs::detail {

struct MemUsageInKilobytes {
  uint64_t total_kb;
  uint64_t used_kb;
};

auto ParseAstlValue(std::string_view token, astl_value_type_t raw_value_type)
    -> std::expected<AstlValue, astl_status_code>;

auto ParseUint64Token(std::string_view token) -> std::expected<uint64_t, astl_status_code>;

auto ReadPrefixedLineTokens(std::string_view contents, std::string_view line_prefix)
    -> std::expected<std::vector<std::string>, astl_status_code>;

auto ReadMemUsageInKilobytes(std::string_view contents) -> std::expected<MemUsageInKilobytes, astl_status_code>;

}  // namespace astl::procfs::detail

#endif  // PROCFS_UTILS_READERS_PRIMITIVES_HPP_
