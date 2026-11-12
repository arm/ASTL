// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "procfs/procfs_system_utils.hpp"

#include <limits>

#include "astl_cxx_compat.hpp"
#include "procfs/procfs_utils_readers.hpp"
#include "procfs/procfs_utils_readers_primitives.hpp"

namespace astl::procfs {
namespace {
constexpr uint64_t kBytesPerMeminfoKilobyte = 1024;
auto               Add(uint64_t& total, uint64_t value) -> bool {
  if (value > std::numeric_limits<uint64_t>::max() - total) {
    return false;
  }
  total += value;
  return true;
}

auto ReadTaskThreads(const std::filesystem::directory_entry& entry, const detail::TaskStatusReader& read_status)
    -> astl::compat::expected<astl::compat::optional<uint64_t>, astl_status_code> {
  std::string contents;
  const auto  status = read_status(entry.path().filename() / "status", contents);
  auto        value  = status == ASTL_STATUS_SUCCESS
                           ? detail::ReadKeyValueField(contents, KeyValueField{"", "Threads", ASTL_VALUE_UINT64})
                           : astl::compat::expected<AstlValue, astl_status_code>{astl::compat::unexpected(status)};
  if (!value) {
    std::error_code error;
    if (value.error() != ASTL_STATUS_OUT_OF_MEMORY && !std::filesystem::exists(entry.path(), error) && !error) {
      return astl::compat::optional<uint64_t>{};  // Observed PID exit: retry the complete scan.
    }
    // A still-visible PID with an unreadable status is not evidence of task churn.
    return astl::compat::unexpected(value.error());
  }
  return astl::compat::optional<uint64_t>{std::get<uint64_t>(value->value)};
}

auto ScanTasks(const FileInterface& files, bool threads, const detail::TaskStatusReader& read_status)
    -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code> {
  auto entries = files.GetSubdirectories();
  if (!entries) {
    return astl::compat::unexpected(entries.error());
  }
  uint64_t count = 0;
  for (const auto& entry : *entries) {
    const auto name = entry.path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }
    if (!threads) {
      ++count;
      continue;
    }
    auto value = ReadTaskThreads(entry, read_status);
    if (!value) {
      return astl::compat::unexpected(value.error());
    }
    if (!*value) {
      return astl::compat::optional<AstlValue>{};
    }
    if (!Add(count, **value)) {
      return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
  }
  return astl::compat::optional<AstlValue>{AstlValue{count}};
}
auto ReadSwapField(std::string_view contents, const SystemField& field)
    -> astl::compat::expected<AstlValue, astl_status_code> {
  const auto total = detail::ReadKeyValueField(contents, KeyValueField{"meminfo", "SwapTotal", ASTL_VALUE_UINT64});
  const auto free  = detail::ReadKeyValueField(contents, KeyValueField{"meminfo", "SwapFree", ASTL_VALUE_UINT64});
  if (!total || !free) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  const auto total_kb = std::get<uint64_t>(total->value);
  const auto free_kb  = std::get<uint64_t>(free->value);
  const auto used_kb  = total_kb > free_kb ? total_kb - free_kb : uint64_t{0};
  if (field.kind == "swap_occupancy") {
    return AstlValue{total_kb == 0 ? 0.0 : 100.0 * (static_cast<double>(used_kb) / static_cast<double>(total_kb))};
  }
  if (used_kb > std::numeric_limits<uint64_t>::max() / kBytesPerMeminfoKilobyte) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return AstlValue{used_kb * kBytesPerMeminfoKilobyte};
}

auto ReadMeminfoSystemField(std::string_view contents, const SystemField& field)
    -> astl::compat::expected<AstlValue, astl_status_code> {
  if (field.kind == "swap_used" || field.kind == "swap_occupancy") {
    return ReadSwapField(contents, field);
  }
  if (field.kind == "swap_total") {
    const auto total = detail::ReadKeyValueField(contents, KeyValueField{"meminfo", "SwapTotal", ASTL_VALUE_UINT64});
    if (!total || std::get<uint64_t>(total->value) > std::numeric_limits<uint64_t>::max() / kBytesPerMeminfoKilobyte) {
      return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    return AstlValue{std::get<uint64_t>(total->value) * kBytesPerMeminfoKilobyte};
  }
  return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}

auto ReadInterruptField(const FileInterface& files, const SystemField& field)
    -> astl::compat::expected<AstlValue, astl_status_code> {
  auto counters = ReadInterruptCounters(files, field.relative_path);
  if (!counters) {
    return astl::compat::unexpected(counters.error());
  }
  const auto it = counters->find(field.label);
  if (it == counters->end()) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return AstlValue{it->second};
}
auto ReadSystemContents(std::string_view contents, const SystemField& field, std::string* source_identity,
                        bool* missing_label) -> astl::compat::expected<AstlValue, astl_status_code> {
  if (field.relative_path == "meminfo") {
    return ReadMeminfoSystemField(contents, field);
  }
  if (field.relative_path == "stat" || field.relative_path == "vmstat") {
    return detail::ReadTokenField(contents, TokenField{field.relative_path, field.kind, 1, ASTL_VALUE_UINT64});
  }
  if (field.relative_path == "net/dev") {
    return detail::ReadNetworkField(contents, field, missing_label);
  }
  if (field.relative_path == "diskstats") {
    return detail::ReadDiskField(contents, field, source_identity, missing_label);
  }
  return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}
}  // namespace

auto detail::ReadTaskCount(const FileInterface& files, bool threads, const TaskStatusReader& read_status)
    -> astl::compat::expected<AstlValue, astl_status_code> {
  constexpr size_t max_attempts = 3;
  for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
    auto result = ScanTasks(files, threads, read_status);
    if (!result) {
      return astl::compat::unexpected(result.error());
    }
    if (*result) {
      return **result;
    }
  }
  return astl::compat::unexpected(ASTL_STATUS_FILE_ERROR);
}

auto ReadSystemField(const FileInterface& files, const SystemField& field, std::string* source_identity,
                     bool* missing_label) -> astl::compat::expected<AstlValue, astl_status_code> {
  if (missing_label) {
    *missing_label = false;
  }
  if (field.kind == "processes" || field.kind == "threads") {
    // Never retain a status descriptor across PID reuse.
    FileInterface task_files{files.GetBasePath(), 0};
    return detail::ReadTaskCount(files, field.kind == "threads",
                                 [&](const auto& path, auto& contents) { return task_files.Read(path, contents); });
  }
  if (field.kind == "interrupts") {
    return ReadInterruptField(files, field);
  }
  std::string contents;
  const auto  status = files.Read(field.relative_path, contents);
  if (status != ASTL_STATUS_SUCCESS) {
    return astl::compat::unexpected(status);
  }
  return ReadSystemContents(contents, field, source_identity, missing_label);
}
}  // namespace astl::procfs
