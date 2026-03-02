# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

set(TARGET "sample_test")

add_executable(${TARGET} sample_test.cpp)
target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_project_warnings(${TARGET})

message("cmake_current_source_dir: ${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(${TARGET} PRIVATE Astl::astl)

if(SHARED_KERNEL)
  target_compile_definitions(${TARGET} PUBLIC USED_SHARED)
endif()

add_custom_target(
  runsampletest
  COMMAND ${TARGET}
  COMMENT "Run sample test")
