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

set(SRCS libsensors_collector_utests.cpp libsensors_metric_builder_utests.cpp libsensors_topology_plugin_utests.cpp
         mock_libsensors.cpp)
set(TARGET "libsensors_test")

if(ASTL_HAVE_LIBSENSORS)
  message(STATUS "libsensors support enabled, building libsensors_collector_utests")
  add_executable(${TARGET} ${SRCS})
  # note - do _not_ link to the real libsensors here, since we're mocking it out with mock_libsensors.cpp
  target_compile_definitions(${TARGET} PRIVATE ASTL_HAVE_SENSORS)

  include(Warnings)
  set_project_warnings(${TARGET})
  if(MSVC)
    # Suppress warning from third-party trompeloeil headers in test-only code paths.
    target_compile_options(${TARGET} PRIVATE /wd4172)
  endif()

  # astl.h and others should come as include paths along with the taret_link_libraries joining Astl::astl_static
  target_link_libraries(
    ${TARGET}
    # test targets should link astl publicly so that gcovr can measure how much of astl_static is covered
    PUBLIC Astl::astl Astl::astl_static
    PRIVATE Catch2::Catch2WithMain)

  # have catch2's cmake module discover the tests and add them to ctest for us
  include(CTest)
  include(Catch)
  catch_discover_tests(
    ${TARGET}
    DISCOVERY_MODE
    PRE_TEST
    ADD_TAGS_AS_LABELS
    PROPERTIES
    ENVIRONMENT
    # prevent valgrind from searching the web and hanging looking for debug symbols
    "ASTL_CONFIG_DIR=${CMAKE_SOURCE_DIR}/tests/config;DEBUGINFOD_URLS=")

else()
  message(STATUS "libsensors support disabled, sensors_collector_utests will not be built")
endif()
