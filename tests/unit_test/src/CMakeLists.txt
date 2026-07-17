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
set(UNIT_TEST_SRCS
    api_layout_utests.cpp
    astl_value_utests.cpp
    astl_utils_utests.cpp
    astl_configuration_utests.cpp
    collector_manager_utests.cpp
    file_interface_utests.cpp
    formula_utests.cpp
    logger_utests.cpp
    orchestrator_utests.cpp
    scmi_ioctl_collector_utests.cpp
    scmi_ioctl_interface_utests.cpp
    scmi_sysfs_collector_utests.cpp
    sampled_value_metric_utests.cpp
    delta_metric_utests.cpp
    rate_metric_utests.cpp
    residency_metric_utests.cpp
    event_metric_utests.cpp
    finite_set_metric_utests.cpp
    config_manager_utests.cpp
    metric_builder_utests.cpp
    metric_manager_utests.cpp
    scmi_specification_json_utests.cpp
    topology_manager_utests.cpp
    output_manager_utests.cpp
    procfs_collector_utests.cpp
    procfs_composite_metric_utests.cpp
    procfs_metric_builder_utests.cpp
    buffer_output_utests.cpp
    processed_sampled_data_utests.cpp
    perfetto_output_utests.cpp
    csv_system_info_utests.cpp
    system_info_utests.cpp
    interval_csv_utests.cpp
    summary_csv_output_utests.cpp
    string_pool_utests.cpp
    protobuf_serdes_utests.cpp
    archive_utests.cpp
    pause_marker_utests.cpp
    lifecycle_events_utests.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_utilities.cpp)
if(ASTL_HAVE_LIBSENSORS)
  find_library(SENSORS_LIB sensors)
  if(SENSORS_LIB)
    message(STATUS "libsensors found, building sensors_collector_utests")
  else()
    message(STATUS "libsensors not found, sensors_collector_utests will not be built")
  endif()
  message(STATUS "libsensors support disabled, sensors_collector_utests will not be built")
endif()
add_executable(unit_test ${UNIT_TEST_SRCS})
target_compile_definitions(unit_test PRIVATE ASTL_BUILD)
include(Warnings)
set_project_warnings(unit_test)
if(MSVC)
  # Suppress warning from third-party trompeloeil headers in test-only code paths.
  target_compile_options(unit_test PRIVATE /wd4172)
endif()
target_include_directories(unit_test PRIVATE "${CMAKE_BINARY_DIR}/src/impl/gen"
                                             "${CMAKE_SOURCE_DIR}/tests/unit_test/include")
# astl.h and others should come as include paths along with the taret_link_libraries joining Astl::astl_static
target_link_libraries(
  unit_test
  # test targets should link astl publicly so that gcovr can measure how much of astl_static is covered
  PUBLIC Astl::astl_static
  PRIVATE Catch2::Catch2WithMain)
add_custom_target(
  rununittests
  COMMAND unit_test
  COMMENT "Run all unit tests")
# have catch2's cmake module discover the tests and add them to ctest for us
include(CTest)
include(Catch)
# unit tests will be run with ASTL_CONFIG_DIR set to the config directory in the build tree
catch_discover_tests(
  unit_test
  DISCOVERY_MODE
  PRE_TEST
  EXTRA_ARGS
  --allow-running-no-tests
  ADD_TAGS_AS_LABELS
  PROPERTIES
  ENVIRONMENT
  "ASTL_CONFIG_DIR=${CMAKE_SOURCE_DIR}/tests/config;DEBUGINFOD_URLS=")

add_executable(pure_c_header_smoke pure_c_header_smoke.c)
set_target_properties(
  pure_c_header_smoke
  PROPERTIES C_STANDARD 99
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
target_include_directories(pure_c_header_smoke PRIVATE "${CMAKE_SOURCE_DIR}/include")
if(MSVC)
  target_compile_options(pure_c_header_smoke PRIVATE /WX /W4)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
  target_compile_options(pure_c_header_smoke PRIVATE -Werror -Wall -Wextra -Wpedantic)
endif()
add_test(NAME pure_c_header_smoke COMMAND pure_c_header_smoke)
