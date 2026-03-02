# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Expects you to have already done: include(macros)
# And also called: SetAstlVersion()
configure_file("${PROJECT_SOURCE_DIR}/include/astl/astl_version.h.in"
               "${PROJECT_BINARY_DIR}/include/astl/astl_version.h")
