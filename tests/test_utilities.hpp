// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_TEST_UTILITIES_H_
#define ASTL_TEST_UTILITIES_H_

#include <filesystem>
#include <memory>

#include "../../utils/astl_utils.hpp"
#include "astl/astl_test_hooks.h"
#include "orchestrator/orchestrator.hpp"

/**
 * @brief RAII guard to remove a temporary file upon destruction
 */
struct TempFileGuard {
  explicit TempFileGuard(const std::filesystem::path& path) : path(path) {}

  // make sure we don't double-remove the filesystem path, forbid copies/moves for now.
  TempFileGuard(TempFileGuard const&)            = delete;
  TempFileGuard& operator=(TempFileGuard const&) = delete;
  TempFileGuard(TempFileGuard&&)                 = delete;
  TempFileGuard& operator=(TempFileGuard&&)      = delete;

  ~TempFileGuard() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  std::filesystem::path path;
};

/**
 * @brief RAII guard to restore an environment variable on scope exit
 */
struct EnvVarGuard {
  // restore original value on destruction
  explicit EnvVarGuard(astl::EnvVar env_var) : env_var(env_var), old_value(astl::GetEnvVar(this->env_var)) {}

  // set new value on construction, and restore original on destruction.
  EnvVarGuard(astl::EnvVar env_var, const std::string& value) : env_var{env_var}, old_value{astl::GetEnvVar(env_var)} {
    astl::SetEnvVar(env_var, value);
  }

  // don't double-unset the environment variable. forbid copies/moves for now.
  EnvVarGuard(EnvVarGuard const&)            = delete;
  EnvVarGuard& operator=(EnvVarGuard const&) = delete;
  EnvVarGuard(EnvVarGuard&&)                 = delete;
  EnvVarGuard& operator=(EnvVarGuard&&)      = delete;

  ~EnvVarGuard() {
    // Restore previous value (empty == unset for codepaths that check GetEnvVar().empty()).
    (void)astl::SetEnvVar(env_var, old_value);
  }

  astl::EnvVar env_var;
  std::string  old_value;
};

/**
 * @brief A test harness construct to replace the ASTL's Orchestrator instance with one for testing
 *
 * This is an RAII-style manager; on construction it will swap out the existing orchestrator
 * with the given `test_orchestrator`. On destruction (usually at the end of a test), it'll swap
 * the original orchestrator back in.
 */
class TestOrchestratorInjector {
 public:
  TestOrchestratorInjector() = delete;  // we must provide an orchestrator to inject

  /**
   * @brief Replace the existing `orchestrator` with the given test_orchestrator.
   *
   * When this TestOrchestratorInjector is destroyed, it'll put the original orchestrator back
   */
  explicit TestOrchestratorInjector(std::unique_ptr<astl::Orchestrator> test_orchestrator) {
    astlInjectTestOrchestrator(test_orchestrator.release(), &_original_orchestrator);
  }

  /**
   * @brief swap the original orchestrator back into the library to resume use as normal
   */
  ~TestOrchestratorInjector() {
    // swap back the original orchestrator, and retrieve the test orchestrator for clean up.
    astl_test_orchestrator_t test_orchestrator_handle{nullptr};
    astlInjectTestOrchestrator(_original_orchestrator, &test_orchestrator_handle);
    // now clean up the `test_orchestrator` we received in this class's constructor
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete static_cast<astl::Orchestrator*>(test_orchestrator_handle);
  }

  // since we're managing a resource (an original orchestrator and test orchestrator),
  // disallow coppying and moving
  TestOrchestratorInjector(TestOrchestratorInjector const&)            = delete;
  TestOrchestratorInjector(TestOrchestratorInjector&&)                 = delete;
  TestOrchestratorInjector& operator=(TestOrchestratorInjector const&) = delete;
  TestOrchestratorInjector& operator=(TestOrchestratorInjector&&)      = delete;

 private:
  // hold the original orchestrator as a raw handle, since that's how the C interface provides it
  astl_test_orchestrator_t _original_orchestrator{nullptr};
};

#endif  // ASTL_TEST_UTILITIES_H_
