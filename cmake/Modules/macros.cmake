# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set(ASTL_CMAKE_MACRO_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. CACHE INTERNAL "")

macro(SetAstlVersion)
  set(ASTL_VERSION_OVERRIDE
      ""
      CACHE STRING "Override the ASTL package version without modifying VERSION.md")
  if(ASTL_VERSION_OVERRIDE)
    set(ASTL_SOURCE_VERSION "${ASTL_VERSION_OVERRIDE}")
  else()
    # Read only the first non-empty line of VERSION.md as the semantic version (e.g. 0.0.1.post)
    file(STRINGS "${ASTL_CMAKE_MACRO_DIR}/VERSION.md" _ver_lines LIMIT_COUNT 1)
    if(NOT _ver_lines)
      message(FATAL_ERROR "VERSION.md is empty or missing a version string on the first line")
    endif()
    set(ASTL_SOURCE_VERSION "${_ver_lines}")
  endif()
  string(STRIP "${ASTL_SOURCE_VERSION}" ASTL_SOURCE_VERSION)
  if(NOT ASTL_SOURCE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(\\.post)?$")
    message(
      FATAL_ERROR
        "ASTL version '${ASTL_SOURCE_VERSION}' must use MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH.post format")
  endif()

  # CMake's project(VERSION) accepts numeric components only. VERSION.md may use
  # a trailing .post marker during development, but the built C API continues to
  # report the corresponding stable semantic version.
  string(REGEX REPLACE "\\.post$" "" ASTL_VERSION "${ASTL_SOURCE_VERSION}")
  # Provide compile definition with quotes to avoid tokenization issues
  add_compile_definitions(ASTL_VERSION="${ASTL_VERSION}")
endmacro()
