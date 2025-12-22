#ifndef ASTL_TEST_UTILITIES_H_
#define ASTL_TEST_UTILITIES_H_

#include <filesystem>

#include "../../utils/astl_utils.hpp"

/**
 * @brief RAII guard to remove a temporary file upon destruction
 */
struct TempFileGuard {
  explicit TempFileGuard(const std::filesystem::path& path) : path(path) {}
  ~TempFileGuard() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  std::filesystem::path path;
};

/**
 * @brief RAII guard to restore an environment variable on scope exit
 */
struct EnvVarGuard {
  explicit EnvVarGuard(std::string name) : name(std::move(name)), old_value(astl::GetEnvVar(this->name)) {}

  ~EnvVarGuard() {
    // Restore previous value (empty == unset for codepaths that check GetEnvVar().empty()).
    (void)astl::SetEnvVar(name, old_value);
  }

  std::string name;
  std::string old_value;
};

#endif  // ASTL_TEST_UTILITIES_H_
