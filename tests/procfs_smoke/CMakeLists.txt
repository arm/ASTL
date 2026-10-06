# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

add_executable(procfs_smoke procfs_smoke.cpp)
target_link_libraries(procfs_smoke PRIVATE Astl::astl)
include(Warnings)
set_project_warnings(procfs_smoke)

# Let deployment scripts follow the actual target paths, including configuration-specific output directories.
file(
  GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/procfs-smoke-$<CONFIG>-binary.txt"
  CONTENT "$<TARGET_FILE:procfs_smoke>")
file(
  GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/procfs-smoke-$<CONFIG>-library-dir.txt"
  CONTENT "$<TARGET_FILE_DIR:astl>")
