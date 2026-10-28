// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/config_source.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>

#ifdef __linux__
#  include <link.h>

#  include <span>
#endif

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include "astl_logger.hpp"
#include "astl_utils.hpp"
#include "config/embedded_config.hpp"

namespace astl::config {
namespace {
auto Key(const std::filesystem::path& path) -> std::string { return path.lexically_normal().generic_string(); }
auto ReadDirectory(const std::filesystem::path& root)
    -> std::expected<std::map<std::string, nlohmann::json>, astl_status_code> {
  try {
    if (!std::filesystem::is_directory(root)) {
      ASTL_LOG_ERROR("Config directory does not exist: {}", root.string());
      return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
    }
    std::map<std::string, nlohmann::json> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") {
        continue;
      }
      std::ifstream input(entry.path());
      input.exceptions(std::ifstream::badbit | std::ifstream::failbit);
      result.emplace(entry.path().lexically_relative(root).generic_string(), nlohmann::json::parse(input));
    }
    return result;
  } catch (const std::exception& error) {
    ASTL_LOG_ERROR("Unable to read config directory {}: {}", root.string(), error.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}

auto ValidateReference(const nlohmann::json& value, const std::string& path,
                       const std::map<std::string, nlohmann::json>& documents) -> astl_status_code {
  if (!value.is_string()) {
    ASTL_LOG_ERROR("Config reference in {} must be a string", path);
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  const std::filesystem::path reference = value.get<std::string>();
  const auto                  resolved  = (std::filesystem::path(path).parent_path() / reference).lexically_normal();
  if (reference.has_root_path() || resolved == ".." || resolved.generic_string().starts_with("../")) {
    ASTL_LOG_ERROR("Config reference in {} escapes its source: {}", path, reference.string());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  if (!documents.contains(resolved.generic_string())) {
    ASTL_LOG_ERROR("Config reference in {} is not in its config tree: {}", path, reference.string());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return ASTL_STATUS_SUCCESS;
}

auto ValidateReferences(const nlohmann::json& doc, const std::string& path,
                        const std::map<std::string, nlohmann::json>& documents) -> astl_status_code {
  if (doc.contains("extends")) {
    if (auto status = ValidateReference(doc["extends"], path, documents); status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  for (const auto* field : {"scmi_uuid_mapping", "uuid_mapping"}) {
    if (!doc.contains(field)) {
      continue;
    }
    for (const auto& entry : doc[field]) {
      if (entry.contains("metrics_file")) {
        if (auto status = ValidateReference(entry["metrics_file"], path, documents); status != ASTL_STATUS_SUCCESS) {
          return status;
        }
      }
      if (entry.contains("specification_file")) {
        if (auto status = ValidateReference(entry["specification_file"], path, documents);
            status != ASTL_STATUS_SUCCESS) {
          return status;
        }
      }
    }
  }
  return ASTL_STATUS_SUCCESS;
}

// This address belongs to the ASTL module, or to the executable in a static build.
auto BinaryPath() -> std::filesystem::path {
#ifdef _WIN32
  HMODULE module = nullptr;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&BinaryPath), &module)) {
    return {};
  }
  std::wstring buffer(32768, L'\0');
  const auto   length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return {};
  }
  buffer.resize(length);
  return std::filesystem::path(buffer);
#else
#  ifdef __linux__
  // The same static archive supplies both the shared library and static clients.
  // Identify the main executable by its loaded segments, not a build-time flag.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto       address       = reinterpret_cast<std::uintptr_t>(&BinaryPath);
  const auto in_executable = dl_iterate_phdr(
      [](dl_phdr_info* module, std::size_t, void* context) {
        if (module->dlpi_name != nullptr && *module->dlpi_name != '\0') {
          return 0;
        }
        const auto target = *static_cast<const std::uintptr_t*>(context);
        for (const auto& segment : std::span(module->dlpi_phdr, module->dlpi_phnum)) {
          const auto start = module->dlpi_addr + segment.p_vaddr;
          if (segment.p_type == PT_LOAD && target >= start && target - start < segment.p_memsz) {
            return 1;
          }
        }
        return 0;
      },
      &address);
  if (in_executable != 0) {
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error);
  }
#  endif
  Dl_info info{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (dladdr(reinterpret_cast<const void*>(&BinaryPath), &info) != 0 && info.dli_fname != nullptr) {
    std::error_code error;
    return std::filesystem::canonical(info.dli_fname, error);
  }
  return {};
#endif
}
// Resolve relative loader paths while the module is being initialized, before
// application code can change the working directory.
const auto kBinaryLocation = BinaryPath();
}  // namespace

auto ConfigSource::Create(const std::filesystem::path& binary_path)
    -> std::expected<std::shared_ptr<const ConfigSource>, astl_status_code> {
  try {
    auto                  source             = std::make_shared<ConfigSource>();
    bool                  explicit_directory = false;
    const auto            configured         = GetEnvVar(EnvVar::ASTL_CONFIG_DIR, &explicit_directory);
    std::filesystem::path directory;
    if (explicit_directory) {
      if (configured.empty()) {
        ASTL_LOG_ERROR("ASTL_CONFIG_DIR is empty");
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      directory = configured;
    } else {
      const auto binary = binary_path.empty() ? kBinaryLocation : binary_path;
      if (!binary.empty() && std::filesystem::is_directory(binary.parent_path() / "config")) {
        directory = binary.parent_path() / "config";
      }
    }
    if (!directory.empty()) {
      auto documents = ReadDirectory(directory);
      if (!documents) {
        return std::unexpected(documents.error());
      }
      if (documents->empty()) {
        ASTL_LOG_ERROR("Config directory contains no JSON resources: {}", directory.string());
        return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
      }
      source->documents_ = std::move(*documents);
    } else {
      for (const auto& resource : EmbeddedResources()) {
        source->documents_.emplace(resource.path, nlohmann::json::parse(resource.data));
      }
    }
    for (const auto& [path, doc] : source->documents_) {
      if (const auto status = ValidateReferences(doc, path, source->documents_); status != ASTL_STATUS_SUCCESS) {
        return std::unexpected(status);
      }
    }
    return source;
  } catch (const std::exception& error) {
    ASTL_LOG_ERROR("Unable to load ASTL configuration: {}", error.what());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
}
auto ConfigSource::Load(const std::filesystem::path& path) const -> std::expected<nlohmann::json, astl_status_code> {
  const auto key   = Key(path);
  const auto found = documents_.find(key);
  if (found != documents_.end()) {
    return found->second;
  }
  ASTL_LOG_ERROR("Config resource not found: {}", path.string());
  return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
}
auto ConfigSource::Exists(const std::filesystem::path& path) const -> bool {
  const auto key = Key(path);
  return documents_.contains(key);
}
auto ConfigSource::Files(const std::filesystem::path& root, bool recursive) const
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> result;
  const auto                         prefix = Key(root) + "/";
  for (const auto& [path, doc] : documents_) {
    if (!path.starts_with(prefix)) {
      continue;
    }
    if (recursive || path.find('/', prefix.size()) == std::string::npos) {
      result.emplace_back(path);
    }
  }
  return result;
}
}  // namespace astl::config
