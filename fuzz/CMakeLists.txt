# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

add_executable(astl_wrapper_validation_fuzzer astl_wrapper_validation_fuzzer.cpp)

find_package(spdlog CONFIG REQUIRED)

target_compile_definitions(astl_wrapper_validation_fuzzer PRIVATE ASTL_BUILD)
target_link_libraries(
  astl_wrapper_validation_fuzzer
  PRIVATE Astl::astl_static astl_fuzz_instrumentation spdlog::spdlog_header_only)
target_link_options(
  astl_wrapper_validation_fuzzer
  PRIVATE -fno-omit-frame-pointer -fno-sanitize-recover=undefined -fsanitize=fuzzer,address,undefined)

include(Warnings)
set_project_warnings(astl_wrapper_validation_fuzzer)
