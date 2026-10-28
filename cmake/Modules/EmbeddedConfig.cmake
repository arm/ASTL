# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
set(ASTL_EMBED_CONFIG_DIR
    "${PROJECT_SOURCE_DIR}/config"
    CACHE PATH "Prepared config tree to embed")
set(ASTL_RELEASE_PROFILE
    "public"
    CACHE STRING "Identity of the embedded release profile")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(astl_resource_args --root "${ASTL_EMBED_CONFIG_DIR}" --output "${PROJECT_BINARY_DIR}/generated/embedded_config.cpp"
                       --profile "${ASTL_RELEASE_PROFILE}")
file(GLOB_RECURSE astl_resource_files FOLLOW_SYMLINKS CONFIGURE_DEPENDS "${ASTL_EMBED_CONFIG_DIR}/*.json")
set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS ${astl_resource_files})
set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/scripts/embed_config.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/embed_config.py" ${astl_resource_args}
                RESULT_VARIABLE astl_resource_result)
if(NOT astl_resource_result EQUAL 0)
  message(FATAL_ERROR "Could not generate ASTL embedded configuration")
endif()
target_sources(${LIBRARY_NAME} PRIVATE "${PROJECT_BINARY_DIR}/generated/embedded_config.cpp")
