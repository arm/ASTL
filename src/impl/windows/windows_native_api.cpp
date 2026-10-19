// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "windows/windows_system_api.hpp"

#if defined(ASTL_INCLUDE_WINDOWS)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <pdh.h>
#  include <pdhmsg.h>
#  include <windows.h>

#  include <cwchar>
#  include <utility>

#  include "astl_logger.hpp"

namespace astl {
namespace {
class PdhQuery {
 public:
  PDH_HQUERY handle{};
  PdhQuery()                                   = default;
  PdhQuery(const PdhQuery&)                    = delete;
  auto operator=(const PdhQuery&) -> PdhQuery& = delete;
  ~PdhQuery() { Close(); }
  void Close() {
    if (handle) {
      PdhCloseQuery(handle);
      handle = nullptr;
    }
  }
  auto Open() -> bool {
    Close();
    return PdhOpenQueryW(nullptr, 0, &handle) == ERROR_SUCCESS;
  }
};

// Translate the English wildcard before expanding paths on localized Windows.
auto LocalizedCpuWildcard(PDH_HQUERY query) -> std::expected<std::wstring, astl_status_code> {
  PDH_HCOUNTER wildcard{};
  const auto   status = PdhAddEnglishCounterW(query, L"\\Processor Information(*)\\% Processor Time", 0, &wildcard);
  if (status != ERROR_SUCCESS) {
    ASTL_LOG_WARNING("Windows CPU performance counters unavailable: {}", status);
    return std::wstring{};
  }
  DWORD bytes{};
  if (PdhGetCounterInfoW(wildcard, FALSE, &bytes, nullptr) != PDH_MORE_DATA) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  std::vector<unsigned char> buffer(bytes);
  auto*                      info = reinterpret_cast<PDH_COUNTER_INFO_W*>(buffer.data());
  if (PdhGetCounterInfoW(wildcard, FALSE, &bytes, info) != ERROR_SUCCESS) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return std::wstring{info->szFullPath};
}

auto ExpandCpuPaths(const std::wstring& wildcard) -> std::expected<std::vector<wchar_t>, astl_status_code> {
  DWORD chars{};
  auto  status = PdhExpandWildCardPathW(nullptr, wildcard.c_str(), nullptr, &chars, 0);
  if (status == PDH_CSTATUS_NO_OBJECT || status == PDH_CSTATUS_NO_COUNTER) {
    return std::vector<wchar_t>{};
  }
  if (status != PDH_MORE_DATA) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  std::vector<wchar_t> paths(chars);
  status = PdhExpandWildCardPathW(nullptr, wildcard.c_str(), paths.data(), &chars, 0);
  if (status != ERROR_SUCCESS) {
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
  return paths;
}

auto ProcessorInstance(const wchar_t* path) -> std::string {
  DWORD bytes{};
  if (PdhParseCounterPathW(path, nullptr, &bytes, 0) != PDH_MORE_DATA) {
    return {};
  }
  std::vector<unsigned char> buffer(bytes);
  auto*                      elements = reinterpret_cast<PDH_COUNTER_PATH_ELEMENTS_W*>(buffer.data());
  if (PdhParseCounterPathW(path, elements, &bytes, 0) != ERROR_SUCCESS || !elements->szInstanceName) {
    return {};
  }
  std::string instance;
  for (const auto* character = elements->szInstanceName; *character; ++character) {
    // Only numeric identities and commas are needed; totals are filtered later.
    instance += (*character >= L'0' && *character <= L'9') || *character == L',' ? static_cast<char>(*character) : '?';
  }
  return instance;
}

auto ProcessorInstances(const std::vector<wchar_t>& paths) -> std::vector<std::string> {
  std::vector<std::string> instances;
  if (!paths.empty()) {
    for (const auto* path = paths.data(); *path != L'\0'; path += std::wcslen(path) + 1) {
      instances.push_back(ProcessorInstance(path));
    }
  }
  return SortWindowsProcessorInstances(std::move(instances));
}

class WindowsSystemApi final : public IWindowsSystemApi {
 public:
  auto DiscoverProcessors() -> std::expected<std::vector<std::string>, astl_status_code> override {
    PdhQuery query;
    if (!query.Open()) {
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    const auto wildcard = LocalizedCpuWildcard(query.handle);
    if (!wildcard) {
      return std::unexpected(wildcard.error());
    }
    if (wildcard->empty()) {
      return std::vector<std::string>{};
    }
    const auto paths = ExpandCpuPaths(*wildcard);
    if (!paths) {
      return std::unexpected(paths.error());
    }
    return ProcessorInstances(*paths);
  }

  auto ConfigureCpu(const std::vector<std::string>& instances) -> astl_status_code override {
    _instances = instances;
    _query.Close();
    _counters.clear();
    _primed = false;
    if (_instances.empty()) {
      return ASTL_STATUS_SUCCESS;
    }
    if (!_query.Open()) {
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    for (const auto& instance : _instances) {
      const std::wstring path =
          L"\\Processor Information(" + std::wstring(instance.begin(), instance.end()) + L")\\% Processor Time";
      PDH_HCOUNTER counter{};
      const auto   status = PdhAddEnglishCounterW(_query.handle, path.c_str(), 0, &counter);
      if (status != ERROR_SUCCESS) {
        ASTL_LOG_ERROR("Cannot add Windows CPU counter '{}': {}", instance, status);
        _query.Close();
        _counters.clear();
        return ASTL_STATUS_BAD_CONFIGURATION;
      }
      _counters.emplace(instance, counter);
    }
    return ASTL_STATUS_SUCCESS;
  }
  auto ResetCpu() -> astl_status_code override {
    // Keep the query registered. Discard its next result, then calculate from two
    // fresh samples so the paused interval cannot leak into utilization.
    _primed = false;
    return ASTL_STATUS_SUCCESS;
  }
  auto ReadCpu() -> std::expected<std::unordered_map<std::string, std::optional<double>>, astl_status_code> override {
    std::unordered_map<std::string, std::optional<double>> samples;
    if (_counters.empty()) {
      return samples;
    }
    const auto status = PdhCollectQueryData(_query.handle);
    if (status != ERROR_SUCCESS) {
      _primed = false;
      ASTL_LOG_WARNING("Windows CPU collection failed: {}", status);
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    for (const auto& [instance, counter] : _counters) {
      std::optional<double> value;
      if (_primed) {
        PDH_FMT_COUNTERVALUE formatted{};
        const auto           result = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &formatted);
        if (result == ERROR_SUCCESS &&
            (formatted.CStatus == PDH_CSTATUS_VALID_DATA || formatted.CStatus == PDH_CSTATUS_NEW_DATA)) {
          value = formatted.doubleValue;
        }
      }
      samples.emplace(instance, value);
    }
    _primed = true;
    return samples;
  }
  auto ReadMemory() -> std::expected<WindowsMemorySnapshot, astl_status_code> override {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
      ASTL_LOG_ERROR("Windows memory collection failed: {}", GetLastError());
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    return WindowsMemorySnapshot{status.ullTotalPhys, status.ullAvailPhys};
  }

 private:
  PdhQuery                                      _query;
  std::vector<std::string>                      _instances;
  std::unordered_map<std::string, PDH_HCOUNTER> _counters;
  bool                                          _primed{false};
};
}  // namespace

auto MakeWindowsSystemApi() -> std::unique_ptr<IWindowsSystemApi> { return std::make_unique<WindowsSystemApi>(); }
}  // namespace astl

#endif  // ASTL_INCLUDE_WINDOWS
