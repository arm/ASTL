# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)
include(CMakeParseArguments)

# Keep the partitioning mechanism independent of how CTest executes the shards.
function(astl_add_catch_shards target)
  cmake_parse_arguments(SHARD "" "KIND;SHARD_COUNT;TEST_SPEC" "ENVIRONMENT" ${ARGN})
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Cannot add Catch2 shards for missing target: ${target}")
  endif()
  if(NOT SHARD_SHARD_COUNT OR SHARD_SHARD_COUNT LESS 1)
    message(FATAL_ERROR "Catch2 shard count for ${target} must be at least 1")
  endif()
  math(EXPR last_shard "${SHARD_SHARD_COUNT} - 1")
  foreach(shard_index RANGE 0 ${last_shard})
    set(test_name "${target}_${SHARD_KIND}_shard_${shard_index}")
    add_test(NAME ${test_name}
             COMMAND $<TARGET_FILE:${target}> --shard-index ${shard_index} --shard-count ${SHARD_SHARD_COUNT} --order
                     lex --reporter TeamCity "${SHARD_TEST_SPEC}" --allow-running-no-tests)
    set_tests_properties(${test_name} PROPERTIES LABELS "${SHARD_KIND}" ENVIRONMENT "${SHARD_ENVIRONMENT}")
  endforeach()
endfunction()

# Coverage replaces ordinary per-case discovery, but preserves separate processes for tests that require isolation and
# integration tests run after the report.
function(astl_discover_tests target)
  if(ENABLE_COVERAGE)
    catch_discover_tests(${target} TEST_SPEC "[integration],[time_sensitive],[valgrind_isolated]" ${ARGN})
  else()
    catch_discover_tests(${target} ${ARGN})
  endif()
endfunction()

function(astl_add_coverage_shards target)
  if(ENABLE_COVERAGE)
    astl_add_catch_shards(${target} KIND coverage TEST_SPEC "~[integration]~[time_sensitive]~[valgrind_isolated]"
                          ${ARGN})
  endif()
endfunction()
