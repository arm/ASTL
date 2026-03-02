# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# set the default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Default CMAKE_BUILD_TYPE not set. Using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

add_executable(multithreaded_e2e_test e2e_test_runner.cpp e2e_test_common.cpp multithreaded_e2e_test.cpp)

target_compile_definitions(multithreaded_e2e_test PRIVATE ASTL_BUILD)

include(Warnings)
set_project_warnings(multithreaded_e2e_test)

# Link against the shared library (astl) to test the external C API
find_package(Catch2 3 REQUIRED)
target_link_libraries(multithreaded_e2e_test PRIVATE Astl::astl Catch2::Catch2WithMain)

# Add a custom target to run the E2E test
add_custom_target(
  run_multithreaded_e2e_test
  COMMAND multithreaded_e2e_test
  COMMENT "Run multi-threaded E2E test with MockSysfs")

# Add the test to CTest but disable by default (requires MockSysfs)
include(CTest)
add_test(NAME multithreaded_e2e_test COMMAND multithreaded_e2e_test)

# Set test properties - mark as E2E test that requires MockSysfs DISABLED by default - use run_e2e.sh script to run with
# proper environment setup
set_tests_properties(multithreaded_e2e_test PROPERTIES LABELS "e2e;multithreaded;mocksysfs" TIMEOUT 60 DISABLED TRUE)
