# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Register a small number of suite-level Catch2 tests for CTest's MemCheck action. The ordinary catch_discover_tests
# registrations remain available for precise reporting in non-Valgrind runs.
include(CMakeParseArguments)

function(astl_add_valgrind_shards target)
  if(NOT ENABLE_VALGRIND)
    return()
  endif()

  set(one_value_args SHARD_COUNT TEST_SPEC)
  set(multi_value_args ENVIRONMENT)
  cmake_parse_arguments(ASTL_MEMCHECK "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT TARGET ${target})
    message(FATAL_ERROR "Cannot add Valgrind shards for missing target: ${target}")
  endif()
  if(NOT ASTL_MEMCHECK_SHARD_COUNT OR ASTL_MEMCHECK_SHARD_COUNT LESS 1)
    message(FATAL_ERROR "Valgrind shard count for ${target} must be at least 1")
  endif()

  math(EXPR last_shard "${ASTL_MEMCHECK_SHARD_COUNT} - 1")
  foreach(shard_index RANGE 0 ${last_shard})
    set(test_name "${target}_valgrind_shard_${shard_index}")
    add_test(NAME ${test_name}
             COMMAND $<TARGET_FILE:${target}> --shard-index ${shard_index} --shard-count ${ASTL_MEMCHECK_SHARD_COUNT}
                     --order lex --reporter TeamCity "${ASTL_MEMCHECK_TEST_SPEC}" --allow-running-no-tests)
    set_tests_properties(${test_name} PROPERTIES LABELS "valgrind" ENVIRONMENT "${ASTL_MEMCHECK_ENVIRONMENT}")
  endforeach()
endfunction()
