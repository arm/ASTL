# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Detects Linux libc implementations from their ioctl request parameter type. glibc uses unsigned long, while musl uses
# int. A compile probe observes the target headers when cross-compiling, unlike host-side runtime inspection.
function(astl_detect_libc)
  set(astl_libc_musl OFF)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)

    check_cxx_source_compiles(
      [[
        #include <sys/ioctl.h>

        using IoctlFunction = int (*)(int, int, ...);
        IoctlFunction ioctl_function = &ioctl;

        int main() { return ioctl_function == nullptr; }
      ]]
      ASTL_IOCTL_REQUEST_IS_INT)

    check_cxx_source_compiles(
      [[
        #include <sys/ioctl.h>

        using IoctlFunction = int (*)(int, unsigned long, ...);
        IoctlFunction ioctl_function = &ioctl;

        int main() { return ioctl_function == nullptr; }
      ]]
      ASTL_IOCTL_REQUEST_IS_UNSIGNED_LONG)

    if(ASTL_IOCTL_REQUEST_IS_INT AND ASTL_IOCTL_REQUEST_IS_UNSIGNED_LONG)
      message(FATAL_ERROR "ioctl request parameter matched both int and unsigned long")
    elseif(ASTL_IOCTL_REQUEST_IS_INT)
      set(astl_libc_musl ON)
      message(STATUS "Detected musl-compatible ioctl signature (request type: int)")
    elseif(ASTL_IOCTL_REQUEST_IS_UNSIGNED_LONG)
      message(STATUS "Detected glibc-compatible ioctl signature (request type: unsigned long)")
    else()
      message(FATAL_ERROR "Unsupported ioctl signature: expected request parameter type int or unsigned long")
    endif()
  endif()

  set(ASTL_LIBC_MUSL
      ${astl_libc_musl}
      PARENT_SCOPE)
endfunction()
