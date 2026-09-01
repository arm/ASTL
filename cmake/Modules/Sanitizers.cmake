# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

set(ASTL_SANITIZER
    "none"
    CACHE STRING "Sanitizer instrumentation: none, address-undefined, or thread")
set_property(CACHE ASTL_SANITIZER PROPERTY STRINGS none address-undefined thread)

function(astl_probe_sanitizer_runtime probe_name sanitizer_flags output_variable)
  set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")

  string(JOIN " " sanitizer_flags_string ${sanitizer_flags})
  set(CMAKE_REQUIRED_FLAGS "${sanitizer_flags_string}")
  set(CMAKE_REQUIRED_LINK_OPTIONS ${sanitizer_flags})

  check_c_source_compiles("int main(void) { return 0; }" "ASTL_${probe_name}_C_RUNTIME_AVAILABLE")
  check_cxx_source_compiles("int main() { return 0; }" "ASTL_${probe_name}_CXX_RUNTIME_AVAILABLE")

  set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")
  set(CMAKE_REQUIRED_LINK_OPTIONS "${saved_required_link_options}")

  if(ASTL_${probe_name}_C_RUNTIME_AVAILABLE AND ASTL_${probe_name}_CXX_RUNTIME_AVAILABLE)
    set(${output_variable}
        ON
        PARENT_SCOPE)
  else()
    set(${output_variable}
        OFF
        PARENT_SCOPE)
  endif()
endfunction()

function(astl_configure_sanitizers)
  string(TOLOWER "${ASTL_SANITIZER}" sanitizer_mode)
  if(NOT sanitizer_mode MATCHES "^(none|address-undefined|thread)$")
    message(FATAL_ERROR "ASTL_SANITIZER must be one of: none, address-undefined, thread")
  endif()

  if(sanitizer_mode STREQUAL "none")
    return()
  endif()

  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "ASTL sanitizer configurations are supported only on Linux")
  endif()
  if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "ASTL sanitizer configurations require upstream LLVM Clang for both C and C++")
  endif()
  if(ENABLE_COVERAGE)
    message(FATAL_ERROR "Sanitizer and coverage instrumentation cannot be enabled together")
  endif()
  if(ENABLE_VALGRIND)
    message(FATAL_ERROR "Sanitizers and Valgrind cannot be enabled together")
  endif()

  if(sanitizer_mode STREQUAL "address-undefined")
    set(sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
    set(sanitizer_link_flags -fsanitize=address,undefined)
    set(probe_name "ASAN_UBSAN")
  else()
    set(sanitizer_flags -fsanitize=thread -fno-omit-frame-pointer -g)
    set(sanitizer_link_flags -fsanitize=thread)
    set(probe_name "TSAN")
  endif()

  astl_probe_sanitizer_runtime("${probe_name}" "${sanitizer_link_flags}" sanitizer_runtime_available)
  if(NOT sanitizer_runtime_available)
    message(FATAL_ERROR "The selected Clang toolchain cannot compile and link ${sanitizer_mode} programs in C and C++")
  endif()

  # MSan needs an instrumented C++ standard library and instrumented binary dependencies. This probe only reports
  # whether the compiler and runtime are present; it does not make the normal ASTL dependency graph MSan-safe.
  astl_probe_sanitizer_runtime("MSAN" "-fsanitize=memory" msan_runtime_available)
  set(ASTL_MSAN_RUNTIME_AVAILABLE
      ${msan_runtime_available}
      CACHE INTERNAL "Whether Clang can compile and link a basic MemorySanitizer program")
  if(msan_runtime_available)
    message(
      STATUS
        "MemorySanitizer runtime probe passed; no preset is provided because ASTL dependencies are not fully instrumented"
    )
  else()
    message(STATUS "MemorySanitizer runtime probe failed for the selected Clang toolchain")
  endif()

  message(STATUS "ASTL sanitizer enabled: ${sanitizer_mode}")
  add_compile_options(${sanitizer_flags})
  add_link_options(${sanitizer_link_flags})
endfunction()
