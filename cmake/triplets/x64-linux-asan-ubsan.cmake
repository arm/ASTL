# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Instrument static vcpkg dependencies with ASan and UBSan for native x86_64 Linux sanitizer builds.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/clang-asan-ubsan.cmake")

set(_astl_asan_ubsan_flags
    "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g")
set(VCPKG_C_FLAGS "${_astl_asan_ubsan_flags}")
set(VCPKG_CXX_FLAGS "${_astl_asan_ubsan_flags}")
set(VCPKG_LINKER_FLAGS "-fsanitize=address,undefined")
