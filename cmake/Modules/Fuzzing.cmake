# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

function(astl_configure_fuzzing)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "ASTL fuzzing is supported only on Linux with upstream LLVM Clang")
  endif()

  if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(
      FATAL_ERROR
        "ASTL fuzzing requires upstream LLVM Clang for both C and C++; configure with the fuzz preset or set CC=clang and CXX=clang++"
    )
  endif()

  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
  check_cxx_source_compiles(
    [[
      #include <cstddef>
      #include <cstdint>
      extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) { return 0; }
    ]]
    ASTL_HAS_LIBFUZZER)
  cmake_pop_check_state()

  if(NOT ASTL_HAS_LIBFUZZER)
    message(
      FATAL_ERROR
        "ASTL fuzzing requires a Clang toolchain with the libFuzzer runtime; install the matching LLVM compiler-rt package and reconfigure with the fuzz preset"
    )
  endif()

  add_library(astl_fuzz_instrumentation INTERFACE)
  target_compile_options(
    astl_fuzz_instrumentation
    INTERFACE -O1
              -g
              -fno-omit-frame-pointer
              -fno-sanitize-recover=undefined
              -fsanitize=fuzzer-no-link,address,undefined)

  target_link_libraries(astl_static PRIVATE astl_fuzz_instrumentation)

  if(TARGET astl)
    target_link_libraries(astl PRIVATE astl_fuzz_instrumentation)
    target_link_options(
      astl
      PRIVATE -fno-omit-frame-pointer -fno-sanitize-recover=undefined -fsanitize=address,undefined)
  endif()
endfunction()
