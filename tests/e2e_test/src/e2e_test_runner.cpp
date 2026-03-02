// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file e2e_test_runner.cpp
 * @brief Main runner for all ASTL E2E tests using Catch2
 */

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <vector>

#include "astl/astl.h"
#include "e2e_test_common.hpp"
#include "multithreaded_e2e_test.hpp"

using astl_test::CheckMockSysfs;

// Global telemetry root path passed from command line
// Mutable global is necessary here because Catch2 TEST_CASE macros are defined at file scope
// and cannot accept runtime parameters directly. This is set once in main() before tests run.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::string g_telemetry_root;

/**
 * @brief Catch2 test cases for ASTL E2E tests
 */
TEST_CASE("Multi-threaded phased execution", "[e2e][multithreaded][nominal]") {
  REQUIRE(!g_telemetry_root.empty());
  REQUIRE(CheckMockSysfs(g_telemetry_root));

  astl_test::TestMultiThreadedEndToEnd();
}

TEST_CASE("Multiple concurrent configure attempts after start", "[e2e][multithreaded][negative]") {
  REQUIRE(!g_telemetry_root.empty());
  REQUIRE(CheckMockSysfs(g_telemetry_root));

  astl_test::TestMultipleConfigureAfterStart();
}

/**
 * @brief Main entry point with Catch2 session
 */
int main(int argc, char* argv[]) {
  // Parse TELEMETRY_ROOT before initializing Catch2
  if (argc < 2) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const char* program_name = (argc > 0) ? argv[0] : "multithreaded_e2e_test";
    std::cerr << "\n❌ Missing required argument: TELEMETRY_ROOT\n";
    std::cerr << "Usage: " << program_name << " <TELEMETRY_ROOT> [catch2-options]\n";
    std::cerr << "\nCatch2 options:\n";
    std::cerr << "  -l, --list-tests         List all tests\n";
    std::cerr << "  -t, --list-tags          List all tags\n";
    std::cerr << "  -s, --success            Show successful tests\n";
    std::cerr << "  -r <format>              Report format (console, xml, junit)\n";
    return 1;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  g_telemetry_root = argv[1];

  std::cout << "=======================================\n";
  std::cout << "ASTL E2E Test Suite (Catch2)\n";
  std::cout << "========================================\n";

  auto version = astlVersion();
  std::cout << "ASTL Version: " << version._major << "." << version._minor << "." << version._micro << "\n";
  std::cout << "TELEMETRY_ROOT: " << g_telemetry_root << "\n\n";

  // Create new argv without TELEMETRY_ROOT for Catch2 using RAII
  std::vector<char*> catch_argv;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  catch_argv.push_back(argv[0]);
  for (int i = 2; i < argc; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    catch_argv.push_back(argv[i]);
  }
  int catch_argc = static_cast<int>(catch_argv.size());

  // Run Catch2 session
  int result = Catch::Session().run(catch_argc, catch_argv.data());

  return result;
}
