// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/configuration_manager.hpp"

#if defined(__linux__) || defined(__APPLE__)
#  include <dlfcn.h>
#elif defined(_WIN32)
#  include <windows.h>
#else
#  error "Unsupported Operating System"
#endif

#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "config/astl_configuration.hpp"

namespace astl {
namespace ConfigurationManager {

namespace fs = std::filesystem;

namespace {
auto GetOverrideMutex() -> std::mutex& {
  static std::mutex override_mtx;
  return override_mtx;
}

auto GetLoadFilePathOverrideStorage() -> std::optional<fs::path>& {
  static std::optional<fs::path> load_file_path_override;
  return load_file_path_override;
}
}  // namespace

auto GetAstlFilePath() -> std::expected<fs::path, astl_status_code> {
#if defined(__linux__) || defined(__APPLE__)
  Dl_info dl_info;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Required to obtain function symbol address
  if (dladdr(reinterpret_cast<void*>(&GetAstlFilePath), &dl_info) == 0) {
    ASTL_LOG_ERROR("Could not find info for shared object when detecting config file path (Linux / Mac)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }

  if (dl_info.dli_fname == nullptr) {
    ASTL_LOG_ERROR("Could not determine path of ASTL .so library (Linux / Mac)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }
  fs::path lib_path{dl_info.dli_fname};
  // If we got an executable (not .so), we're likely statically linked
  // Try to find the library in a relative path (useful for tests)
  // @todo(ASTL-274) look up config files from resource / appdata paths rather or in addition to lib path.
  if (lib_path.extension() != ".so" && !lib_path.filename().string().starts_with("lib")) {
    ASTL_LOG_DEBUG("Detected statically linked binary: {}", lib_path.string());
    // Look for lib directory relative to executable
    auto potential_lib_dir = lib_path.parent_path().parent_path() / "lib";
    if (fs::exists(potential_lib_dir)) {
      ASTL_LOG_INFO("Using library path from build tree: {}", potential_lib_dir.string());
      return potential_lib_dir / "libastl.so";
    }
  }
  return lib_path;
#elif defined(_WIN32)
  HMODULE h_module = nullptr;
  if (!GetModuleHandleExA(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Required to obtain function symbol address
          reinterpret_cast<LPCSTR>(&GetAstlFilePath), &h_module)) {
    ASTL_LOG_ERROR("Could not find info for shared object when detecting config file path (Windows)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }
  char  so_path[MAX_PATH];
  DWORD path_length = GetModuleFileNameA(h_module, so_path, MAX_PATH);
  if (path_length == 0 || path_length == MAX_PATH) {
    ASTL_LOG_ERROR("Could not determine path of ASTL .so library (Windows)");
    return std::unexpected<astl_status_code>(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return fs::path(so_path);
#endif
}

auto GetConfiguration() -> std::expected<AstlConfiguration, astl_status_code> {
  return AstlConfiguration::CreateConfiguration();
};

auto SetLoadFilePathOverride(const std::optional<fs::path>& load_file_path) -> void {
  std::scoped_lock lock{GetOverrideMutex()};
  GetLoadFilePathOverrideStorage() = load_file_path;
}

auto GetLoadFilePathOverride() -> std::optional<fs::path> {
  std::scoped_lock lock{GetOverrideMutex()};
  return GetLoadFilePathOverrideStorage();
}

}  // namespace ConfigurationManager

}  // namespace astl
