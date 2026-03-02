# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set(ASTL_CMAKE_MACRO_DIR ${CMAKE_CURRENT_LIST_DIR}/../.. CACHE INTERNAL "")

macro(SetAstlVersion)
  # Read only the first non-empty line of VERSION.md as the semantic version (e.g. 0.0.1)
  file(STRINGS "${ASTL_CMAKE_MACRO_DIR}/VERSION.md" _ver_lines LIMIT_COUNT 1)
  if(NOT _ver_lines)
    message(FATAL_ERROR "VERSION.md is empty or missing a version string on the first line")
  endif()
  set(ASTL_VERSION "${_ver_lines}")
  string(STRIP "${ASTL_VERSION}" ASTL_VERSION)
  # Provide compile definition with quotes to avoid tokenization issues
  add_compile_definitions(ASTL_VERSION="${ASTL_VERSION}")
endmacro()

