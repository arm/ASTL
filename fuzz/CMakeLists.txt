# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

add_library(astl_fuzz_fixtures STATIC fixtures/minimal_procfs_fixture.cpp)
target_include_directories(astl_fuzz_fixtures PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

include(Warnings)
set_project_warnings(astl_fuzz_fixtures)

add_executable(astl_wrapper_validation_fuzzer astl_wrapper_validation_fuzzer.cpp)
add_executable(astl_discovery_fuzzer astl_discovery_fuzzer.cpp)

set(ASTL_FUZZ_DISCOVERY_PROFILE
    "input"
    CACHE STRING "Default deterministic fixture profile for the discovery fuzzer")
set_property(CACHE ASTL_FUZZ_DISCOVERY_PROFILE PROPERTY STRINGS input procfs procfs-scmi)
if(NOT ASTL_FUZZ_DISCOVERY_PROFILE MATCHES "^(input|procfs|procfs-scmi)$")
  message(FATAL_ERROR "ASTL_FUZZ_DISCOVERY_PROFILE must be input, procfs, or procfs-scmi")
endif()

find_package(spdlog CONFIG REQUIRED)

target_compile_definitions(astl_wrapper_validation_fuzzer PRIVATE ASTL_BUILD)
target_link_libraries(
  astl_wrapper_validation_fuzzer
  PRIVATE Astl::astl_static astl_fuzz_instrumentation spdlog::spdlog_header_only)
target_link_options(
  astl_wrapper_validation_fuzzer
  PRIVATE -fno-omit-frame-pointer -fno-sanitize-recover=undefined -fsanitize=fuzzer,address,undefined)

set_project_warnings(astl_wrapper_validation_fuzzer)

target_compile_definitions(astl_discovery_fuzzer PRIVATE ASTL_BUILD)
target_compile_definitions(astl_discovery_fuzzer PRIVATE ASTL_FUZZ_DISCOVERY_PROFILE="${ASTL_FUZZ_DISCOVERY_PROFILE}")
target_link_libraries(
  astl_discovery_fuzzer
  PRIVATE Astl::astl_static astl_fuzz_fixtures astl_fuzz_instrumentation spdlog::spdlog_header_only)
target_link_options(
  astl_discovery_fuzzer
  PRIVATE -fno-omit-frame-pointer -fno-sanitize-recover=undefined -fsanitize=fuzzer,address,undefined)

add_custom_command(
  TARGET astl_discovery_fuzzer
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/config"
          "$<TARGET_FILE_DIR:astl_discovery_fuzzer>/config"
  COMMENT "Staging ASTL configuration for astl_discovery_fuzzer")

set_project_warnings(astl_discovery_fuzzer)
