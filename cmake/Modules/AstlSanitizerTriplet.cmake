# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Select a native vcpkg triplet before project() loads the vcpkg toolchain. This keeps the public sanitizer preset
# architecture-neutral while ensuring ASTL and its native dependencies use the same instrumentation.
function(astl_configure_sanitizer_vcpkg_triplet)
  if(NOT ASTL_SANITIZER STREQUAL "address-undefined")
    return()
  endif()

  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The debug-asan-ubsan preset is supported only on Linux")
  endif()

  set(astl_host_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  if(astl_host_processor STREQUAL "")
    # CMAKE_HOST_SYSTEM_PROCESSOR is not initialized until project(), but the vcpkg triplet must be selected before
    # project() loads the toolchain. OS_PLATFORM provides the native architecture during this early configure phase.
    cmake_host_system_information(RESULT astl_host_processor QUERY OS_PLATFORM)
  endif()
  string(TOLOWER "${astl_host_processor}" astl_host_processor)
  if(astl_host_processor MATCHES "^(aarch64|arm64)$")
    set(astl_vcpkg_architecture "arm64")
  elseif(astl_host_processor MATCHES "^(amd64|x64|x86_64)$")
    set(astl_vcpkg_architecture "x64")
  else()
    message(
      FATAL_ERROR
        "The debug-asan-ubsan preset supports native aarch64 and x86_64 hosts; detected ${astl_host_processor}")
  endif()

  set(astl_vcpkg_host_triplet "${astl_vcpkg_architecture}-linux")
  set(astl_vcpkg_target_triplet "${astl_vcpkg_architecture}-linux-asan-ubsan")
  set(astl_vcpkg_triplet_file "${CMAKE_SOURCE_DIR}/cmake/triplets/${astl_vcpkg_target_triplet}.cmake")
  if(NOT EXISTS "${astl_vcpkg_triplet_file}")
    message(FATAL_ERROR "Missing sanitizer vcpkg triplet: ${astl_vcpkg_triplet_file}")
  endif()

  set(VCPKG_HOST_TRIPLET
      "${astl_vcpkg_host_triplet}"
      CACHE STRING "Native vcpkg host triplet selected for the sanitizer build" FORCE)
  set(VCPKG_TARGET_TRIPLET
      "${astl_vcpkg_target_triplet}"
      CACHE STRING "Native vcpkg target triplet selected for the sanitizer build" FORCE)
  message(STATUS "Sanitizer vcpkg triplets: host=${VCPKG_HOST_TRIPLET}, target=${VCPKG_TARGET_TRIPLET}")
endfunction()
