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

add_executable(wrapper_test wrapper_test.cpp metric_groups_tests.cpp metric_state_discovery_tests.cpp
                            summary_api_tests.cpp thread_safety_tests.cpp crop_api_tests.cpp)

if(MSVC)
  # Ensure third-party trompeloeil warning suppression is applied on every wrapper test TU.
  foreach(wrapper_src IN ITEMS wrapper_test.cpp metric_groups_tests.cpp metric_state_discovery_tests.cpp
                               summary_api_tests.cpp thread_safety_tests.cpp crop_api_tests.cpp)
    set_source_files_properties(${wrapper_src} APPEND_STRING PROPERTY COMPILE_FLAGS " /wd4172")
  endforeach()
endif()

target_compile_definitions(wrapper_test PRIVATE ASTL_BUILD)

include(Warnings)
set_project_warnings(wrapper_test)
if(MSVC)
  # Suppress warning from third-party trompeloeil headers in test-only code paths.
  target_compile_options(wrapper_test PRIVATE /wd4172)
endif()

# astl.h and others should come as include paths along with the target_link_libraries joining Astl::astl_static
target_link_libraries(
  wrapper_test
  # test targets should link astl publicly so that gcovr can measure how much of astl_static is covered
  PUBLIC Astl::astl_static
  PRIVATE Catch2::Catch2WithMain)
add_custom_target(
  runwrappertests
  COMMAND wrapper_test
  COMMENT "Run wrapper test")

# have catch2's cmake module discover the tests and add them to ctest for us
include(CTest)
include(Catch)

# tests will be run with ASTL_CONFIG_DIR set to the config directory in the build tree
catch_discover_tests(
  wrapper_test
  DISCOVERY_MODE
  PRE_TEST
  ADD_TAGS_AS_LABELS
  PROPERTIES
  ENVIRONMENT
  "ASTL_CONFIG_DIR=${CMAKE_SOURCE_DIR}/tests/config;DEBUGINFOD_URLS=")
