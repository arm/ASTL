# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Keep vcpkg's dependency builds on the same compiler family as ASTL's
# debug-asan-ubsan preset. The sanitizer flags themselves belong in the
# triplet, so they also contribute to vcpkg's package ABI.
set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE FILEPATH "")

# A chain-loaded toolchain replaces vcpkg's platform toolchain, including its
# usual Linux -fPIC initialization. Keep static vcpkg archives suitable for
# ASTL's shared-library link and apply the instrumentation to dependency source
# files themselves.
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "")
set(_astl_dependency_sanitizer_flags
    "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${_astl_dependency_sanitizer_flags}" CACHE STRING "")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_astl_dependency_sanitizer_flags}" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -fsanitize=address,undefined" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT} -fsanitize=address,undefined" CACHE STRING "")
