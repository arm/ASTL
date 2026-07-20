# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.0...4.0)

# sensors_collector_test exercises the SensorsCollector class which depends on libsensors. we'll be mocking out the
# sensors.h library calls in these unit tests, by _linking_ to a mock implementation, so I'm separating these tests out
# from the main unit_test target which might sometimes use the real libsensors

find_package(Catch2 CONFIG REQUIRED)

project(SENSORS_COLLECTOR_TEST LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

if(NOT TARGET Astl::astl_static)
  find_package(astl_static REQUIRED)
endif()

add_subdirectory(src)
