# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Modules/")
cmake_minimum_required(VERSION 3.0...4.0)
project(SAMPLE_TEST)
include(Warnings)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()
option(ASTL_STATIC_LINK "Link sample_test against the static ASTL library (.a) instead of the shared library (.so)" OFF)
if(NOT TARGET Astl::astl)
  find_package(astl REQUIRED)
endif()
add_subdirectory(src)
# ctest will add the sample_test executable to its list of tests to use this, after building the project, `cd build &&
# ctest` PRs should trigger this as well from the `integration.yml` workflow
add_test(NAME SampleTest COMMAND $<TARGET_FILE:sample_test>)
add_test(NAME SampleTest_Immediate COMMAND $<TARGET_FILE:sample_test> --immediate)
add_test(NAME SampleTest_Interval COMMAND $<TARGET_FILE:sample_test> --interval=50)
set_tests_properties(
  SampleTest SampleTest_Immediate SampleTest_Interval PROPERTIES LABELS "integration;sample" ENVIRONMENT
                                                                 "ASTL_CONFIG_DIR=${CMAKE_SOURCE_DIR}/tests/config")
