// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MINIMAL_PROCFS_FIXTURE_HPP_
#define MINIMAL_PROCFS_FIXTURE_HPP_

#include <array>
#include <cstdint>
#include <filesystem>

namespace astl::fuzz {

/** Deterministic values rendered into the minimal procfs fixture. */
// The named fields make these deliberately simple synthetic sample values self-describing.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
struct ProcfsFixtureSnapshot {
  std::array<uint64_t, 10> cpu{100, 0, 20, 880, 0, 0, 0, 0, 0, 0};
  std::array<uint64_t, 10> cpu0{100, 0, 20, 880, 0, 0, 0, 0, 0, 0};
  uint64_t                 mem_total_kib{1024};
  uint64_t                 mem_free_kib{256};
  uint64_t                 mem_available_kib{512};
  uint64_t                 load_1m_hundredths{25};
  uint64_t                 load_5m_hundredths{50};
  uint64_t                 load_15m_hundredths{75};
  uint64_t                 runnable_tasks{1};
  uint64_t                 total_tasks{8};
  uint64_t                 last_pid{42};
  uint64_t                 uptime_hundredths{100000};
  uint64_t                 idle_hundredths{80000};
};
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/**
 * A tiny synthetic procfs tree for deterministic public-API fuzzing.
 *
 * The fixture owns its root, contains only stat, meminfo, loadavg, and uptime,
 * and is read-only between calls to Reset(). A canonical fuzz target can keep
 * one instance for the process and reset its values before every iteration.
 */
class MinimalProcfsFixture {
 public:
  explicit MinimalProcfsFixture(std::filesystem::path root_path);
  ~MinimalProcfsFixture();

  MinimalProcfsFixture(const MinimalProcfsFixture&)            = delete;
  MinimalProcfsFixture& operator=(const MinimalProcfsFixture&) = delete;
  MinimalProcfsFixture(MinimalProcfsFixture&&)                 = delete;
  MinimalProcfsFixture& operator=(MinimalProcfsFixture&&)      = delete;

  void Reset(const ProcfsFixtureSnapshot& snapshot = {});

  [[nodiscard]] auto RootPath() const -> const std::filesystem::path& { return root_path_; }

 private:
  std::filesystem::path root_path_;
};

}  // namespace astl::fuzz

#endif  // MINIMAL_PROCFS_FIXTURE_HPP_
