// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/system_info.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#if defined(__GLIBC__)
#  include <gnu/libc-version.h>
#endif
#if defined(__linux__)
#  include <sys/utsname.h>
#endif
#if defined(__unix__)
#  include <unistd.h>
#endif

#include "astl_logger.hpp"
#include "common/key_value_text_utils.hpp"
#include "common/procfs_utils.hpp"
#include "common/text_file_utils.hpp"
#include "common/text_parse_utils.hpp"
#include "nlohmann/json.hpp"

namespace astl {
namespace {

namespace fs = std::filesystem;

struct HugePagesInfo {
  int64_t total{-1};
  int64_t size_kb{-1};
};

auto ToInt64(uint64_t value) -> std::optional<int64_t> {
  if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<int64_t>(value);
}

auto ParseHugePagesSummary(std::string_view summary) -> HugePagesInfo {
  HugePagesInfo info{};

  constexpr std::string_view total_prefix{"total="};
  if (const auto total_pos = summary.find(total_prefix); total_pos != std::string_view::npos) {
    const auto parsed_total = text::ParseLeadingUint64(summary.substr(total_pos + total_prefix.size()));
    if (parsed_total.has_value()) {
      info.total = ToInt64(*parsed_total).value_or(-1);
    }
  }

  constexpr std::string_view size_prefix{"size_kb="};
  if (const auto size_pos = summary.find(size_prefix); size_pos != std::string_view::npos) {
    const auto parsed_size = text::ParseLeadingUint64(summary.substr(size_pos + size_prefix.size()));
    if (parsed_size.has_value()) {
      info.size_kb = ToInt64(*parsed_size).value_or(-1);
    }
  }

  return info;
}

auto AsciiLower(std::string_view value) -> std::string {
  std::string normalized;
  normalized.reserve(value.size());
  std::transform(value.begin(), value.end(), std::back_inserter(normalized),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return normalized;
}

auto LookupCpuImplementerName(std::string_view implementer) -> std::string_view {
  if (AsciiLower(implementer) == "0x41") {
    return "Arm";
  }
  return {};
}

auto BuildCpuTypeFromArmStyleFields(const text::KeyValueMap& values) -> std::string {
  const auto implementer  = text::FindFirstKeyValue(values, {"CPU implementer"});
  const auto architecture = text::FindFirstKeyValue(values, {"CPU architecture"});
  const auto part         = text::FindFirstKeyValue(values, {"CPU part"});
  const auto variant      = text::FindFirstKeyValue(values, {"CPU variant"});
  const auto revision     = text::FindFirstKeyValue(values, {"CPU revision"});
  const auto field_values = std::array<std::string_view, 5>{implementer, architecture, part, variant, revision};

  if (std::ranges::all_of(field_values, [](std::string_view value) { return value.empty(); })) {
    return {};
  }

  std::ostringstream summary;
  if (const auto implementer_name = LookupCpuImplementerName(implementer); !implementer_name.empty()) {
    summary << implementer_name;
  } else if (!implementer.empty()) {
    summary << "CPU implementer " << implementer;
  } else {
    summary << "CPU";
  }

  if (!part.empty()) {
    summary << " part " << part;
  }

  bool has_details   = false;
  auto append_detail = [&](std::string_view label, const std::string& value) {
    if (value.empty()) {
      return;
    }
    if (!has_details) {
      summary << " (";
      has_details = true;
    } else {
      summary << ", ";
    }
    summary << label << ' ' << value;
  };

  append_detail("arch", architecture);
  append_detail("variant", variant);
  append_detail("revision", revision);

  if (has_details) {
    summary << ')';
  }

  return summary.str();
}

auto IsNameWithNumericSuffix(const std::string& name, std::string_view prefix) -> bool {
  if (name.rfind(prefix, 0) != 0 || name.size() == prefix.size()) {
    return false;
  }
  return std::all_of(name.begin() + static_cast<std::string::difference_type>(prefix.size()), name.end(),
                     [](unsigned char character) { return std::isdigit(character) != 0; });
}

auto ReadSysconfUint32(int name) -> uint32_t {
#if defined(__unix__)
  const auto value = sysconf(name);
  if (value > 0 && value <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
    return static_cast<uint32_t>(value);
  }
#else
  (void)name;
#endif
  return 0;
}

auto ReadCpuCount() -> uint32_t {
#if defined(_SC_NPROCESSORS_CONF)
  auto count = ReadSysconfUint32(_SC_NPROCESSORS_CONF);
  // cppcheck-suppress knownConditionTrueFalse - depends on compile-time configuration
  if (count != 0U) {
    return count;
  }
#endif
#if defined(_SC_NPROCESSORS_ONLN)
  return ReadSysconfUint32(_SC_NPROCESSORS_ONLN);
#else
  return 0;
#endif
}

auto ReadMemoryTotalBytes() -> uint64_t {
  auto mem_total = text::ReadFirstMatchingKeyValueFromFile("/proc/meminfo", {"MemTotal"});
  if (!mem_total.has_value()) {
    return 0;
  }
  auto           total_kb          = text::ParseLeadingUint64(*mem_total);
  constexpr auto kilobytes_in_byte = 1024U;
  if (!total_kb.has_value() || *total_kb > std::numeric_limits<uint64_t>::max() / kilobytes_in_byte) {
    ASTL_LOG_WARNING("Failed to parse total memory from meminfo: value out of range");
    return 0;
  }
  return *total_kb * kilobytes_in_byte;
}

auto ReadCacheLineSizeFromSysfs() -> uint32_t {
  const fs::path               cache_root{"/sys/devices/system/cpu/cpu0/cache"};
  std::error_code              error;
  fs::directory_iterator       iter{cache_root, error};
  const fs::directory_iterator end{};
  while (!error && iter != end) {
    const auto& entry = *iter;
    const auto  name  = entry.path().filename().string();
    if (entry.is_directory(error) && IsNameWithNumericSuffix(name, "index")) {
      auto value = text::ReadFirstTrimmedLine(entry.path() / "coherency_line_size");
      if (value.has_value()) {
        auto parsed = text::ParseUint64(*value);
        if (parsed.has_value() && *parsed <= std::numeric_limits<uint32_t>::max()) {
          return static_cast<uint32_t>(*parsed);
        }
      }
    }
    iter.increment(error);
  }
  ASTL_LOG_WARNING("Error reading cache line from sysfs: %s", error.message().c_str());
  return 0;
}

auto ReadCacheLineSize() -> uint32_t {
#if defined(_SC_LEVEL1_DCACHE_LINESIZE)
  auto line_size = ReadSysconfUint32(_SC_LEVEL1_DCACHE_LINESIZE);
  // cppcheck-suppress knownConditionTrueFalse - depends on compile-time configuration
  if (line_size != 0U) {
    return line_size;
  }
#endif
  return ReadCacheLineSizeFromSysfs();
}

auto ReadCacheInfo() -> std::string {
  const fs::path               cache_root{"/sys/devices/system/cpu/cpu0/cache"};
  std::error_code              error;
  fs::directory_iterator       iter{cache_root, error};
  const fs::directory_iterator end{};
  std::set<std::string>        entries;

  while (!error && iter != end) {
    const auto& entry = *iter;
    const auto  name  = entry.path().filename().string();
    if (entry.is_directory(error) && IsNameWithNumericSuffix(name, "index")) {
      auto level = text::ReadFirstTrimmedLine(entry.path() / "level");
      auto type  = text::ReadFirstTrimmedLine(entry.path() / "type");
      auto size  = text::ReadFirstTrimmedLine(entry.path() / "size");
      if (level.has_value() && type.has_value() && size.has_value() && (*type == "Data" || *type == "Unified")) {
        entries.insert("L" + *level + " " + *type + " " + *size);
      }
    }
    iter.increment(error);
  }

  std::ostringstream summary;
  for (const auto& entry : entries) {
    if (summary.tellp() > 0) {
      summary << ", ";
    }
    summary << entry;
  }
  return summary.str();
}

auto CountMatchingDirectories(const fs::path& root, std::string_view prefix) -> uint32_t {
  std::error_code              error;
  fs::directory_iterator       iter{root, error};
  const fs::directory_iterator end{};
  uint32_t                     count{0};
  while (!error && iter != end) {
    const auto& entry = *iter;
    const auto  name  = entry.path().filename().string();
    if (entry.is_directory(error) && IsNameWithNumericSuffix(name, prefix) &&
        count < std::numeric_limits<uint32_t>::max()) {
      ++count;
    }
    iter.increment(error);
  }
  return count;
}

auto ReadNumaNodeCount(uint32_t core_count) -> uint32_t {
  const auto count = CountMatchingDirectories("/sys/devices/system/node", "node");
  if (count != 0U) {
    return count;
  }
  ASTL_LOG_WARNING("Error reading NUMA node count from sysfs, estimating based on core_count");
  return core_count == 0U ? 0U : 1U;
}

auto ReadSocketCount() -> uint32_t {
  const fs::path               cpu_root{"/sys/devices/system/cpu"};
  std::error_code              error;
  fs::directory_iterator       iter{cpu_root, error};
  const fs::directory_iterator end{};
  std::set<uint64_t>           packages;

  while (!error && iter != end) {
    const auto& entry = *iter;
    const auto  name  = entry.path().filename().string();
    if (entry.is_directory(error) && IsNameWithNumericSuffix(name, "cpu")) {
      auto package_id = text::ReadFirstTrimmedLine(entry.path() / "topology" / "physical_package_id");
      if (package_id.has_value()) {
        auto parsed = text::ParseUint64(*package_id);
        if (parsed.has_value()) {
          packages.insert(*parsed);
        }
      }
    }
    iter.increment(error);
  }

  return packages.size() <= std::numeric_limits<uint32_t>::max() ? static_cast<uint32_t>(packages.size()) : 0U;
}

auto ReadLibcVersion() -> std::string {
#if defined(__GLIBC__)
  return std::string{"glibc "} + gnu_get_libc_version();
#else
  return {};
#endif
}

auto ReadBootInfo() -> std::string {
  std::error_code error;
  if (fs::exists("/sys/firmware/efi", error)) {
    return "UEFI";
  }
  error.clear();
  if (fs::exists("/proc/device-tree", error) || fs::exists("/sys/firmware/devicetree/base", error)) {
    return "device-tree";
  }
  return {};
}

auto ReadHugePages() -> HugePagesInfo {
  auto total = text::ReadFirstMatchingKeyValueFromFile("/proc/meminfo", {"HugePages_Total"});
  if (!total.has_value()) {
    return {};
  }
  auto total_count = text::ParseLeadingUint64(*total);
  if (!total_count.has_value()) {
    return {};
  }
  HugePagesInfo info{};
  info.total = ToInt64(*total_count).value_or(-1);
  auto size  = text::ReadFirstMatchingKeyValueFromFile("/proc/meminfo", {"Hugepagesize"});
  if (size.has_value()) {
    auto size_kb = text::ParseLeadingUint64(*size);
    if (size_kb.has_value()) {
      info.size_kb = ToInt64(*size_kb).value_or(-1);
    }
  }
  return info;
}

auto ParseOsReleaseValue(std::string_view raw_value) -> std::string {
  auto value = procfs::Trim(raw_value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value.remove_prefix(1);
    value.remove_suffix(1);
  }
  return std::string{value};
}

auto ReadOsReleaseValue(const std::string& key) -> std::optional<std::string> {
  auto value = text::ReadFirstMatchingKeyValueFromFile("/etc/os-release", {key}, '=');
  if (!value.has_value()) {
    return std::nullopt;
  }

  auto parsed = ParseOsReleaseValue(*value);
  if (parsed.empty()) {
    return std::nullopt;
  }

  return parsed;
}

}  // namespace

namespace detail {

auto InferCpuTypeFromCpuInfoText(std::string_view cpuinfo_contents) -> std::string {
  const auto values = text::ParseKeyValueText(cpuinfo_contents);

  if (const auto cpu_type = text::FindFirstKeyValue(values, {"model name", "Processor", "Hardware"});
      !cpu_type.empty()) {
    return cpu_type;
  }

  return BuildCpuTypeFromArmStyleFields(values);
}

}  // namespace detail

namespace {

auto CapturePlatformInfoFromSystem() -> PlatformInfoData {
  PlatformInfoData info{};

  info.soc_name =
      text::ReadFirstAvailableLine({"/sys/devices/soc0/machine", "/sys/devices/soc0/family", "/proc/device-tree/model",
                                    "/sys/firmware/devicetree/base/model", "/sys/devices/virtual/dmi/id/product_name"})
          .value_or("");

  info.vendor_id =
      text::ReadFirstAvailableLine({"/sys/devices/soc0/vendor", "/sys/devices/virtual/dmi/id/sys_vendor"}).value_or("");

  info.firmware_version =
      text::ReadFirstAvailableLine({"/sys/devices/virtual/dmi/id/bios_version", "/proc/device-tree/firmware/version",
                                    "/sys/firmware/devicetree/base/firmware/version"})
          .value_or("");

  info.os_name = ReadOsReleaseValue("PRETTY_NAME").value_or("");
  if (info.os_name.empty()) {
    info.os_name = ReadOsReleaseValue("NAME").value_or("");
  }

  info.cpu_type     = text::ReadTextFile("/proc/cpuinfo").transform(detail::InferCpuTypeFromCpuInfoText).value_or("");
  info.cpu_features = text::ReadFirstMatchingKeyValueFromFile("/proc/cpuinfo", {"Features", "flags"}).value_or("");
  info.cache_info   = ReadCacheInfo();
  info.core_count   = ReadCpuCount();
  info.numa_node_count        = ReadNumaNodeCount(info.core_count);
  info.socket_count           = ReadSocketCount();
  info.cache_line_size_bytes  = ReadCacheLineSize();
  info.memory_total_bytes     = ReadMemoryTotalBytes();
  info.libc_version           = ReadLibcVersion();
  info.boot_info              = ReadBootInfo();
  const auto huge_pages_info  = ReadHugePages();
  info.huge_pages_total       = huge_pages_info.total;
  info.huge_page_size_kb      = huge_pages_info.size_kb;
  info.transparent_huge_pages = text::ReadFirstTrimmedLine("/sys/kernel/mm/transparent_hugepage/enabled").value_or("");

#if defined(__linux__)
  struct utsname uts_info{};
  if (uname(&uts_info) == 0) {
    info.kernel_name    = static_cast<const char*>(uts_info.sysname);
    info.kernel_version = static_cast<const char*>(uts_info.version);
    info.kernel_release = static_cast<const char*>(uts_info.release);
    info.hostname       = static_cast<const char*>(uts_info.nodename);
    info.architecture   = static_cast<const char*>(uts_info.machine);
  }
#endif

  return info;
}

auto BuildPlatformInfoJson(const PlatformInfoData& info) -> std::expected<nlohmann::json, astl_status_code> {
  try {
    nlohmann::json payload;
    payload["soc_name"]               = info.soc_name;
    payload["vendor_id"]              = info.vendor_id;
    payload["os_name"]                = info.os_name;
    payload["kernel_name"]            = info.kernel_name;
    payload["kernel_version"]         = info.kernel_version;
    payload["kernel_release"]         = info.kernel_release;
    payload["firmware_version"]       = info.firmware_version;
    payload["hostname"]               = info.hostname;
    payload["architecture"]           = info.architecture;
    payload["cpu_type"]               = info.cpu_type;
    payload["cpu_features"]           = info.cpu_features;
    payload["cache_info"]             = info.cache_info;
    payload["core_count"]             = info.core_count;
    payload["numa_node_count"]        = info.numa_node_count;
    payload["socket_count"]           = info.socket_count;
    payload["cache_line_size_bytes"]  = info.cache_line_size_bytes;
    payload["memory_total_bytes"]     = info.memory_total_bytes;
    payload["libc_version"]           = info.libc_version;
    payload["boot_info"]              = info.boot_info;
    payload["huge_pages_total"]       = info.huge_pages_total;
    payload["huge_page_size_kb"]      = info.huge_page_size_kb;
    payload["transparent_huge_pages"] = info.transparent_huge_pages;
    return payload;
  } catch (const nlohmann::json::exception& e) {
    ASTL_LOG_ERROR("BuildPlatformInfoJson: failed serializing platform info: {}", e.what());
    return std::unexpected{ASTL_STATUS_INTERNAL_ERROR};
  }
}

auto ParsePlatformInfoJson(const nlohmann::json& payload) -> std::expected<PlatformInfoData, astl_status_code> {
  try {
    PlatformInfoData info{};
    info.soc_name              = payload.value("soc_name", "");
    info.vendor_id             = payload.value("vendor_id", "");
    info.os_name               = payload.value("os_name", "");
    info.kernel_name           = payload.value("kernel_name", "");
    info.kernel_version        = payload.value("kernel_version", "");
    info.kernel_release        = payload.value("kernel_release", "");
    info.firmware_version      = payload.value("firmware_version", "");
    info.hostname              = payload.value("hostname", "");
    info.architecture          = payload.value("architecture", "");
    info.cpu_type              = payload.value("cpu_type", "");
    info.cpu_features          = payload.value("cpu_features", "");
    info.cache_info            = payload.value("cache_info", "");
    info.core_count            = payload.value("core_count", payload.value("cpu_count", uint32_t{0}));
    info.numa_node_count       = payload.value("numa_node_count", uint32_t{0});
    info.socket_count          = payload.value("socket_count", uint32_t{0});
    info.cache_line_size_bytes = payload.value("cache_line_size_bytes", uint32_t{0});
    info.memory_total_bytes    = payload.value("memory_total_bytes", uint64_t{0});
    info.libc_version          = payload.value("libc_version", "");
    info.boot_info             = payload.value("boot_info", "");
    info.huge_pages_total      = payload.value("huge_pages_total", int64_t{-1});
    info.huge_page_size_kb     = payload.value("huge_page_size_kb", int64_t{-1});
    if ((info.huge_pages_total < 0 || info.huge_page_size_kb < 0) && payload.contains("huge_pages")) {
      const auto legacy_huge_pages = ParseHugePagesSummary(payload.value("huge_pages", ""));
      if (info.huge_pages_total < 0) {
        info.huge_pages_total = legacy_huge_pages.total;
      }
      if (info.huge_page_size_kb < 0) {
        info.huge_page_size_kb = legacy_huge_pages.size_kb;
      }
    }
    info.transparent_huge_pages = payload.value("transparent_huge_pages", "");
    return info;
  } catch (const nlohmann::json::exception& e) {
    ASTL_LOG_ERROR("ParsePlatformInfoJson: failed parsing platform info json: {}", e.what());
    return std::unexpected{ASTL_STATUS_INTERNAL_ERROR};
  }
}

auto PlatformInfoMutex() -> std::mutex& {
  static std::mutex platform_info_mutex;
  return platform_info_mutex;
}

auto LoadedPlatformInfoPointerStorage() -> std::shared_ptr<const PlatformInfoData>* {
  static std::shared_ptr<const PlatformInfoData> loaded_platform_info_ptr = nullptr;
  return &loaded_platform_info_ptr;
}

}  // namespace

auto GetHostPlatformInfo() -> const PlatformInfoData& {
  static std::once_flag   init_once;
  static PlatformInfoData captured_info;
  std::call_once(init_once, [] { captured_info = CapturePlatformInfoFromSystem(); });
  return captured_info;
}

auto GetLoadedPlatformInfo() -> std::shared_ptr<const PlatformInfoData> {
  std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
  return *LoadedPlatformInfoPointerStorage();
}

auto GetActivePlatformInfo() -> const PlatformInfoData& {
  auto loaded_info = GetLoadedPlatformInfo();
  if (loaded_info != nullptr) {
    return *loaded_info;
  }
  return GetHostPlatformInfo();
}

auto SavePlatformInfoToCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code {
  std::error_code err_code;
  std::filesystem::create_directories(cache_dir_path, err_code);
  if (err_code) {
    ASTL_LOG_ERROR("SavePlatformInfoToCacheDir: failed creating cache directory '{}': {}", cache_dir_path.string(),
                   err_code.message());
    return ASTL_STATUS_FILE_ERROR;
  }

  const auto    output_file = cache_dir_path / kPlatformInfoFileName;
  std::ofstream stream(output_file, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    ASTL_LOG_ERROR("SavePlatformInfoToCacheDir: failed opening '{}'", output_file.string());
    return ASTL_STATUS_FILE_OPEN_FAILED;
  }

  const auto payload_or_error = BuildPlatformInfoJson(GetActivePlatformInfo());
  if (!payload_or_error.has_value()) {
    return payload_or_error.error();
  }
  const auto& payload = payload_or_error.value();
  stream << payload.dump();
  if (stream.fail()) {
    ASTL_LOG_ERROR("SavePlatformInfoToCacheDir: failed writing '{}'", output_file.string());
    return ASTL_STATUS_FILE_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

auto LoadPlatformInfoFromCacheDir(const std::filesystem::path& cache_dir_path) -> astl_status_code {
  const auto input_file = cache_dir_path / kPlatformInfoFileName;

  if (!std::filesystem::exists(input_file)) {
    std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
    *LoadedPlatformInfoPointerStorage() = {};
    ASTL_LOG_WARNING("LoadPlatformInfoFromCacheDir: '{}' not present; using host info", input_file.string());
    return ASTL_STATUS_SUCCESS;
  }

  std::ifstream stream(input_file, std::ios::binary | std::ios::in);
  if (!stream.is_open()) {
    ASTL_LOG_ERROR("LoadPlatformInfoFromCacheDir: failed opening '{}'", input_file.string());
    return ASTL_STATUS_FILE_OPEN_FAILED;
  }

  nlohmann::json payload;
  try {
    stream >> payload;
  } catch (const std::exception& e) {
    ASTL_LOG_ERROR("LoadPlatformInfoFromCacheDir: invalid platform info in '{}': {}", input_file.string(), e.what());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto expected_loaded = ParsePlatformInfoJson(payload);
  if (!expected_loaded.has_value()) {
    return expected_loaded.error();
  }
  auto loaded = expected_loaded.value();

  std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
  *LoadedPlatformInfoPointerStorage() = std::make_shared<const PlatformInfoData>(std::move(loaded));
  return ASTL_STATUS_SUCCESS;
}

auto ClearLoadedPlatformInfo() -> void {
  std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
  *LoadedPlatformInfoPointerStorage() = nullptr;
}

}  // namespace astl
