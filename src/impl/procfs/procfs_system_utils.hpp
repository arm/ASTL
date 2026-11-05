// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_SYSTEM_UTILS_HPP_
#define PROCFS_SYSTEM_UTILS_HPP_

#include <functional>

#include "procfs/procfs_utils.hpp"

namespace astl::procfs::detail {
inline constexpr size_t kDiskStatsMinTokenCount = 14;

using TaskStatusReader = std::function<astl_status_code(const std::filesystem::path&, std::string&)>;
// Retry complete scans after observed PID disappearance; never return an incomplete sum.
auto ReadTaskCount(const FileInterface& files, bool threads, const TaskStatusReader& read_status)
    -> std::expected<AstlValue, astl_status_code>;

auto ReadDiskField(std::string_view contents, const SystemField& field, std::string* source_identity,
                   bool* missing_label = nullptr) -> std::expected<AstlValue, astl_status_code>;
auto ReadNetworkField(std::string_view contents, const SystemField& field, bool* missing_label = nullptr)
    -> std::expected<AstlValue, astl_status_code>;
}  // namespace astl::procfs::detail

#endif  // PROCFS_SYSTEM_UTILS_HPP_
