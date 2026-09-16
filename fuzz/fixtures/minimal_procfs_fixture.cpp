// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "minimal_procfs_fixture.hpp"

#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace astl::fuzz {
namespace {

constexpr auto kReadOnlyFilePermissions =
    std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read;
constexpr auto kReadOnlyDirectoryPermissions = kReadOnlyFilePermissions | std::filesystem::perms::owner_exec |
                                               std::filesystem::perms::group_exec | std::filesystem::perms::others_exec;
constexpr uint64_t kHundredthsPerUnit = 100;

void ThrowOnError(const std::error_code& error, std::string_view operation) {
  if (error) {
    throw std::filesystem::filesystem_error{std::string{operation}, error};
  }
}

void WriteReadOnlyFile(const std::filesystem::path& path, std::string_view contents) {
  std::error_code error;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, error);
  if (error && std::filesystem::exists(path)) {
    ThrowOnError(error, "make procfs fixture file writable");
  }

  std::ofstream output{path, std::ios::out | std::ios::trunc};  // NOLINT(cppcoreguidelines-init-variables)
  if (!output || !(output << contents)) {
    throw std::runtime_error{"unable to write procfs fixture file: " + path.string()};
  }
  output.close();

  error.clear();
  std::filesystem::permissions(path, kReadOnlyFilePermissions, std::filesystem::perm_options::replace, error);
  ThrowOnError(error, "make procfs fixture file read-only");
}

template <std::size_t Size>
void AppendValues(std::ostringstream& output, const std::array<uint64_t, Size>& values) {
  for (const auto value : values) {
    output << ' ' << value;
  }
  output << '\n';
}

auto Hundredths(uint64_t value) -> std::string {
  std::ostringstream output{};
  output.imbue(std::locale::classic());
  output << value / kHundredthsPerUnit << '.' << std::setw(2) << std::setfill('0') << value % kHundredthsPerUnit;
  return output.str();
}

}  // namespace

MinimalProcfsFixture::MinimalProcfsFixture(std::filesystem::path root_path) : root_path_{std::move(root_path)} {
  std::error_code error;
  if (std::filesystem::exists(root_path_, error)) {
    throw std::runtime_error{"procfs fixture root already exists: " + root_path_.string()};
  }
  ThrowOnError(error, "inspect procfs fixture root");
  std::filesystem::create_directories(root_path_, error);
  ThrowOnError(error, "create procfs fixture root");
  Reset();
}

MinimalProcfsFixture::~MinimalProcfsFixture() {
  std::error_code error;
  std::filesystem::permissions(root_path_, std::filesystem::perms::owner_all, std::filesystem::perm_options::add,
                               error);
  for (const auto* filename : {"stat", "meminfo", "loadavg", "uptime"}) {
    error.clear();
    std::filesystem::permissions(root_path_ / filename, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add, error);
  }
  error.clear();
  std::filesystem::remove_all(root_path_, error);
}

void MinimalProcfsFixture::Reset(const ProcfsFixtureSnapshot& snapshot) {
  std::error_code error;
  std::filesystem::permissions(root_path_, std::filesystem::perms::owner_all, std::filesystem::perm_options::add,
                               error);
  ThrowOnError(error, "make procfs fixture root writable");

  std::ostringstream stat;
  stat << "cpu";
  AppendValues(stat, snapshot.cpu);
  stat << "cpu0";
  AppendValues(stat, snapshot.cpu0);
  WriteReadOnlyFile(root_path_ / "stat", stat.str());

  std::ostringstream meminfo;
  meminfo << "MemTotal: " << snapshot.mem_total_kib << " kB\n"
          << "MemFree: " << snapshot.mem_free_kib << " kB\n"
          << "MemAvailable: " << snapshot.mem_available_kib << " kB\n";
  WriteReadOnlyFile(root_path_ / "meminfo", meminfo.str());

  const auto loadavg = Hundredths(snapshot.load_1m_hundredths) + " " + Hundredths(snapshot.load_5m_hundredths) + " " +
                       Hundredths(snapshot.load_15m_hundredths) + " " + std::to_string(snapshot.runnable_tasks) + "/" +
                       std::to_string(snapshot.total_tasks) + " " + std::to_string(snapshot.last_pid) + "\n";
  WriteReadOnlyFile(root_path_ / "loadavg", loadavg);

  const auto uptime = Hundredths(snapshot.uptime_hundredths) + " " + Hundredths(snapshot.idle_hundredths) + "\n";
  WriteReadOnlyFile(root_path_ / "uptime", uptime);

  error.clear();
  std::filesystem::permissions(root_path_, kReadOnlyDirectoryPermissions, std::filesystem::perm_options::replace,
                               error);
  ThrowOnError(error, "make procfs fixture root read-only");
}

}  // namespace astl::fuzz
