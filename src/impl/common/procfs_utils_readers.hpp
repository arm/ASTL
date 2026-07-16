// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_UTILS_READERS_HPP_
#define PROCFS_UTILS_READERS_HPP_

#include <expected>
#include <string_view>
#include <vector>

#include "common/procfs_utils.hpp"

namespace astl::procfs::detail {

auto ReadKeyValueField(std::string_view contents, const KeyValueField& field)
    -> std::expected<AstlValue, astl_status_code>;

auto ReadTokenField(std::string_view contents, const TokenField& field) -> std::expected<AstlValue, astl_status_code>;

auto ReadSplitTokenField(std::string_view contents, const SplitTokenField& field)
    -> std::expected<AstlValue, astl_status_code>;

auto ReadTokenSumField(std::string_view contents, const TokenSumField& field)
    -> std::expected<AstlValue, astl_status_code>;

auto ReadMemUsedField(std::string_view contents, const MemUsedField& field)
    -> std::expected<AstlValue, astl_status_code>;

auto ReadMemUsedPercentField(std::string_view contents, const MemUsedPercentField& field)
    -> std::expected<AstlValue, astl_status_code>;

auto ParseCpuSnapshotFromContents(std::string_view contents, std::string_view line_prefix)
    -> std::expected<CpuSnapshot, astl_status_code>;

auto ParseCpuSnapshotsFromContents(std::string_view contents) -> std::expected<CpuSnapshotMap, astl_status_code>;

}  // namespace astl::procfs::detail

#endif  // PROCFS_UTILS_READERS_HPP_
