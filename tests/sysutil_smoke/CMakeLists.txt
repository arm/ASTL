# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

add_executable(sysutil_smoke sysutil_smoke.cpp)
target_link_libraries(sysutil_smoke PRIVATE Astl::astl)
include(Warnings)
set_project_warnings(sysutil_smoke)

# Let deployment scripts follow the actual target paths, including configuration-specific output directories.
file(
  GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/sysutil-smoke-$<CONFIG>-binary.txt"
  CONTENT "$<TARGET_FILE:sysutil_smoke>")
file(
  GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/sysutil-smoke-$<CONFIG>-library-dir.txt"
  CONTENT "$<TARGET_FILE_DIR:astl>")
