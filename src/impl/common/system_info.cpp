#include "common/system_info.hpp"

#include <fstream>
#include <mutex>
#include <optional>
#include <string>

#if defined(__linux__)
#  include <sys/utsname.h>
#endif

#include "astl_logger.hpp"
#include "nlohmann/json.hpp"

namespace astl {
namespace {

auto Trim(const std::string& value) -> std::string {
  const auto first = value.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r");
  return value.substr(first, (last - first) + 1);
}

auto ReadFirstLine(const char* path) -> std::optional<std::string> {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return std::nullopt;
  }

  std::string line;
  if (!std::getline(stream, line)) {
    return std::nullopt;
  }

  line = Trim(line);
  if (line.empty()) {
    return std::nullopt;
  }

  return line;
}

auto ReadFirstAvailableLine(const std::initializer_list<const char*>& paths) -> std::optional<std::string> {
  for (const char* path : paths) {
    auto value = ReadFirstLine(path);
    if (value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

auto ParseOsReleaseValue(const std::string& raw_value) -> std::string {
  auto value = Trim(raw_value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

auto ReadOsReleaseValue(const std::string& key) -> std::optional<std::string> {
  std::ifstream stream("/etc/os-release");
  if (!stream.is_open()) {
    return std::nullopt;
  }

  std::string line;
  const auto  key_prefix = key + "=";
  while (std::getline(stream, line)) {
    if (line.rfind(key_prefix, 0) != 0) {
      continue;
    }

    auto value = ParseOsReleaseValue(line.substr(key_prefix.size()));
    if (value.empty()) {
      return std::nullopt;
    }

    return value;
  }

  return std::nullopt;
}

auto CapturePlatformInfoFromSystem() -> PlatformInfoData {
  PlatformInfoData info{};

  info.soc_name =
      ReadFirstAvailableLine({"/sys/devices/soc0/machine", "/sys/devices/soc0/family", "/proc/device-tree/model",
                              "/sys/firmware/devicetree/base/model", "/sys/devices/virtual/dmi/id/product_name"})
          .value_or("");

  info.vendor_id =
      ReadFirstAvailableLine({"/sys/devices/soc0/vendor", "/sys/devices/virtual/dmi/id/sys_vendor"}).value_or("");

  info.firmware_version =
      ReadFirstAvailableLine({"/sys/devices/virtual/dmi/id/bios_version", "/proc/device-tree/firmware/version",
                              "/sys/firmware/devicetree/base/firmware/version"})
          .value_or("");

  info.os_name = ReadOsReleaseValue("PRETTY_NAME").value_or("");
  if (info.os_name.empty()) {
    info.os_name = ReadOsReleaseValue("NAME").value_or("");
  }

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

auto BuildPlatformInfoJson(const PlatformInfoData& info) -> nlohmann::json {
  nlohmann::json payload;
  payload["soc_name"]         = info.soc_name;
  payload["vendor_id"]        = info.vendor_id;
  payload["os_name"]          = info.os_name;
  payload["kernel_name"]      = info.kernel_name;
  payload["kernel_version"]   = info.kernel_version;
  payload["kernel_release"]   = info.kernel_release;
  payload["firmware_version"] = info.firmware_version;
  payload["hostname"]         = info.hostname;
  payload["architecture"]     = info.architecture;
  return payload;
}

auto ParsePlatformInfoJson(const nlohmann::json& payload) -> PlatformInfoData {
  PlatformInfoData info{};
  info.soc_name         = payload.value("soc_name", "");
  info.vendor_id        = payload.value("vendor_id", "");
  info.os_name          = payload.value("os_name", "");
  info.kernel_name      = payload.value("kernel_name", "");
  info.kernel_version   = payload.value("kernel_version", "");
  info.kernel_release   = payload.value("kernel_release", "");
  info.firmware_version = payload.value("firmware_version", "");
  info.hostname         = payload.value("hostname", "");
  info.architecture     = payload.value("architecture", "");
  return info;
}

auto PlatformInfoMutex() -> std::mutex& {
  static std::mutex platform_info_mutex;
  return platform_info_mutex;
}

auto LoadedPlatformInfoStorage() -> std::optional<PlatformInfoData>& {
  static std::optional<PlatformInfoData> loaded_platform_info;
  return loaded_platform_info;
}

}  // namespace

auto GetActivePlatformInfo() -> const PlatformInfoData& {
  {
    std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
    if (LoadedPlatformInfoStorage().has_value()) {
      return *LoadedPlatformInfoStorage();
    }
  }

  static std::once_flag   init_once;
  static PlatformInfoData captured_info;
  std::call_once(init_once, [] { captured_info = CapturePlatformInfoFromSystem(); });
  return captured_info;
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

  const auto payload = BuildPlatformInfoJson(GetActivePlatformInfo());
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
    LoadedPlatformInfoStorage().reset();
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

  auto loaded = ParsePlatformInfoJson(payload);

  std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
  LoadedPlatformInfoStorage() = std::move(loaded);
  return ASTL_STATUS_SUCCESS;
}

auto ClearLoadedPlatformInfo() -> void {
  std::lock_guard<std::mutex> lock{PlatformInfoMutex()};
  LoadedPlatformInfoStorage().reset();
}

}  // namespace astl
