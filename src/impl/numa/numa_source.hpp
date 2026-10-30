// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef NUMA_SOURCE_HPP_
#define NUMA_SOURCE_HPP_
#include <array>
#include <map>
#include <optional>
#include <string>

#include "astl_file_interface.hpp"
namespace astl::numa {
// Field order matches the five page counters; percentages use CPU locality, not policy hits.
inline constexpr std::array kFields{"numa_hit", "numa_miss", "interleave_hit", "local_node", "other_node"};
inline constexpr uint64_t   kBasisPointsPerPercent = 100;
inline constexpr uint64_t   kBasisPointsPerWhole   = 10000;
using Counts                                       = std::array<uint64_t, kFields.size()>;
using Snapshot                                     = std::map<std::string, Counts>;
auto Parse(std::string_view text) -> std::expected<Counts, astl_status_code>;
auto Discover(const FileInterface& files) -> std::expected<std::vector<std::string>, astl_status_code>;
auto Read(const FileInterface& files, const std::vector<std::string>& nodes)
    -> std::expected<Snapshot, astl_status_code>;
// Missing/new nodes or a counter reset invalidate this interval rather than underflowing.
auto Delta(const Snapshot& previous, const Snapshot& current, const std::string& node) -> std::optional<Counts>;
}  // namespace astl::numa
#endif
