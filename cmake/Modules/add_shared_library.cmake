# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

function(astl_add_shared_library)
  set(library_name "${PROJECT_NAME}")

  add_library(${library_name} SHARED)
  add_library(Astl::${library_name} ALIAS ${library_name})

  # Create a symlink to the configuration JSON next to the built shared library.
  add_custom_command(
    TARGET ${library_name}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E create_symlink ${PROJECT_SOURCE_DIR}/samples/sample_configuration/astl_configuration.json
      $<TARGET_FILE_DIR:${library_name}>/astl_configuration.json
    COMMENT "Creating symlink to astl_configuration.json in shared library output directory"
    VERBATIM)

  include(Warnings)
  set_project_warnings(${library_name})

  target_compile_definitions(${library_name} PRIVATE ASTL_BUILD)
  if(JSON_DIAGNOSTICS)
    add_compile_definitions(JSON_DIAGNOSTICS=1)
    target_compile_definitions(${library_name} PRIVATE JSON_DIAGNOSTICS=1)
  endif()

  target_sources(
    ${library_name}
    PRIVATE "${PROJECT_SOURCE_DIR}/src/astl_errors.cpp" "${PROJECT_SOURCE_DIR}/src/astl_telemetry.cpp"
            "${PROJECT_SOURCE_DIR}/src/astl_test_hooks.cpp" "${PROJECT_SOURCE_DIR}/src/astl_version.cpp"
            "${PROJECT_SOURCE_DIR}/utils/astl_read_file_handle_cache.cpp")

  target_include_directories(
    ${library_name}
    PRIVATE ${PROJECT_SOURCE_DIR}
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/utils>
           $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  target_link_libraries(${library_name} PRIVATE Astl::astl_static)

  if(UNIX AND NOT APPLE)
    target_link_options(${library_name} PRIVATE $<$<CONFIG:Debug>:-rdynamic>)
    target_link_libraries(${library_name} PRIVATE $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:dl>)
  endif()

  find_package(spdlog CONFIG REQUIRED)
  target_link_libraries(${library_name} PRIVATE spdlog::spdlog_header_only)

  set(public_headers
      "${PROJECT_SOURCE_DIR}/include/astl/astl.h" "${PROJECT_SOURCE_DIR}/include/astl/astl_errors.h"
      "${PROJECT_SOURCE_DIR}/include/astl/astl_telemetry.h" "${PROJECT_BINARY_DIR}/include/astl/astl_version.h")
  set_target_properties(
    ${library_name}
    PROPERTIES PUBLIC_HEADER "${public_headers}"
               DEBUG_POSTFIX "d"
               OUTPUT_NAME "astl-${PROJECT_VERSION_MAJOR}")

  if(UNIX)
    find_program(BASH_PROGRAM bash REQUIRED)
    set(astl_runtime_config_dir "$<TARGET_FILE_DIR:${library_name}>/config")
    add_custom_target(
      ${library_name}_publish_configs ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory ${astl_runtime_config_dir}
      COMMAND ${BASH_PROGRAM} ${PROJECT_SOURCE_DIR}/scripts/publish_configs.sh -o ${astl_runtime_config_dir}
      COMMENT "Publishing ASTL config files to ${astl_runtime_config_dir}"
      VERBATIM)
    add_dependencies(${library_name} ${library_name}_publish_configs)
  endif()

  install(
    TARGETS ${library_name}
    EXPORT "${PROJECT_NAME}Targets"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
    INCLUDES
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endfunction()
