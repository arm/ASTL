# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Register a small number of suite-level Catch2 tests for CTest's MemCheck action. The ordinary catch_discover_tests
# registrations remain available for precise reporting in non-Valgrind runs.
include(AstlCatchShards)

function(astl_add_valgrind_shards target)
  if(NOT ENABLE_VALGRIND)
    return()
  endif()

  astl_add_catch_shards(${target} KIND valgrind ${ARGN})
endfunction()
