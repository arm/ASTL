# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# build a static library `astl_static` that's easy to link with the shared library `astl` or with unit tests. useful for
# providing access to internal functions and classes of the implementation, without exporting everything across the
# shared library interface

set(LIBRARY_NAME "${PROJECT_NAME}_static")
add_library(${LIBRARY_NAME} STATIC)
# add an alias for the library for unambiguous FindPackage
add_library(Astl::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})
target_compile_definitions(${LIBRARY_NAME} PRIVATE ASTL_BUILD)

include(Warnings)
set_project_warnings(${LIBRARY_NAME})

target_sources(
  ${LIBRARY_NAME}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/orchestrator/orchestrator.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/orchestrator/orchestrator_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/collector/collector_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/collector/collector_manager.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/collector/scmi_sysfs_collector.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/astl_configuration.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/configuration_manager.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/metric_json_declaration.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/procfs_metric_json_declaration.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/scmi_metric_json_declaration.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/scmi_platform_telemetry_spec.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/topology/topology_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/topology/topology_manager.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/common/astl_value.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/common/string_pool.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/common/system_info.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/libsensors/libsensors_metric_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/collector/procfs_collector.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/procfs_composite_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/procfs_metric_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/procfs_metric_builder_helpers.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/procfs_metric_builder_expansion_helpers.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/procfs_metric_builder_parsing_helpers.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/operation/procfs_operation_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/procfs_counter_discovery.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/common/procfs_utils.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/common/procfs_utils_readers.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/common/procfs_utils_readers_primitives.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/counter.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/metric_group.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/metric_manager.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/metric_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/raw_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/sampled_value_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/delta_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/rate_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/raw_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/residency_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/finite_set_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/event_metric.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/formula_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/metric/expression_formula.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/output_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/output_manager.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/buffer_output.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/summarizer.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/summary_output.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/perfetto_output.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/csv_system_info.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/interval_csv_output.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/output/summary_csv_output.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/operation/scmi_operation_builder.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/target.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/serdes/serdes_raw_samples.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/serdes/serdes_topology_manager.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/serdes/serdes_metrics.cpp"
          # tinyexpr++ for formulas.
          "${PROJECT_SOURCE_DIR}/third_party/tinyexpr-plusplus/tinyexpr.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/serdes/archive_utils.cpp"
          # for unit tests
          "${PROJECT_SOURCE_DIR}/src/astl_errors.cpp"
          "${PROJECT_SOURCE_DIR}/src/astl_telemetry.cpp"
          "${PROJECT_SOURCE_DIR}/src/astl_test_hooks.cpp"
          "${PROJECT_SOURCE_DIR}/src/astl_version.cpp")

# Disable noisy warnings from third - party tinyexpr++ code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_source_files_properties("${PROJECT_SOURCE_DIR}/third_party/tinyexpr-plusplus/tinyexpr.cpp"
                              PROPERTIES COMPILE_OPTIONS "-Wno-conversion")
elseif(MSVC)
  set_source_files_properties("${PROJECT_SOURCE_DIR}/third_party/tinyexpr-plusplus/tinyexpr.cpp"
                              PROPERTIES COMPILE_OPTIONS "/wd4146;/wd4702")
endif()

# Enable uint64 mode for tinyexpr++ for exact integer arithmetic Enable bitwise operators so &and | work as bitwise
# operators instead of logical operators
target_compile_definitions(${LIBRARY_NAME} PRIVATE TE_UINT64 TE_BITWISE_OPERATORS)
message(STATUS "Using TE_UINT64 for tinyexpr++ (exact uint64_t arithmetic)")
message(STATUS "Using TE_BITWISE_OPERATORS for tinyexpr++ (& and | are bitwise)")

if(ASTL_INCLUDE_LIBSENSORS)
  target_sources(
    ${LIBRARY_NAME}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libsensors/libsensors_api.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/libsensors/libsensors_collector.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/libsensors/libsensors_operation_builder.cpp")

  target_link_libraries(${LIBRARY_NAME} PRIVATE dl)
endif()

# include directories
target_include_directories(
  ${LIBRARY_NAME}
  PUBLIC # where top-level project will look for the library's public headers this static library should make a lot
         # public so unit tests can instantiate objects defined in these directories.
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/utils>
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
         # where external projects will look for the library's public headers
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# Include third - party tinyexpr++ as SYSTEM to suppress warnings from its headers
target_include_directories(${LIBRARY_NAME} SYSTEM
                           PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/tinyexpr-plusplus>)

# include nlohmann_json - json parser for importing configuration files
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${LIBRARY_NAME} PRIVATE nlohmann_json::nlohmann_json)

find_package(miniz CONFIG REQUIRED)
target_link_libraries(${LIBRARY_NAME} PRIVATE miniz::miniz)

# if doing a debug build, include detailed json diagnostics on parse errors This setting has two parts : -the CMake
# cache variable JSON_DIAGNOSTICS, which is set in the CmakePresets.json file and used here in the conditional below -
# the preprocessor definition JSON_DIAGNOSTICS which nlohmann::json looks for at compile time.
if(JSON_DIAGNOSTICS)
  target_compile_definitions(${LIBRARY_NAME} PRIVATE JSON_DIAGNOSTICS=1)
endif()

target_compile_definitions(${LIBRARY_NAME} PRIVATE ASTL_BUILD)

find_package(Protobuf REQUIRED)

set(PROTO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/serdes/raw_samples.proto" "${CMAKE_CURRENT_SOURCE_DIR}/serdes/targets.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/serdes/metrics.proto" "${CMAKE_CURRENT_SOURCE_DIR}/serdes/serdes_common.proto")
set(GEN_DIR "${CMAKE_BINARY_DIR}/src/impl/gen")
file(MAKE_DIRECTORY "${GEN_DIR}")

# Generate .pb.cc and .pb.h
# cmake-format: off
protobuf_generate(
  LANGUAGE
  cpp
  OUT_VAR
  GEN_FILES
  PROTOC_OUT_DIR
  "${GEN_DIR}"
  PROTOS
  ${PROTO_FILES})
# cmake-format: on

# Separate target to just run protoc
add_custom_target(
  proto_gen
  DEPENDS ${GEN_FILES}
  COMMENT "Generate protobuf source files")

target_sources(${LIBRARY_NAME} PRIVATE ${GEN_FILES})

target_include_directories(${LIBRARY_NAME} PRIVATE "${GEN_DIR}" ${Protobuf_INCLUDE_DIRS})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(${GEN_FILES} PROPERTIES COMPILE_OPTIONS "-Wno-nullability-extension;")
endif()

# Protobuf-generated code can trigger `-Wconversion` and `-Wnullability-extension`; suppress these warnings for
# generated sources and serdes_metrics.cpp.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/serdes/serdes_metrics.cpp" ${GEN_FILES}
                              PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-nullability-extension")
elseif(MSVC)
  # Suppress noisy warnings from protobuf - generated code and includes when compiling on MSVC : -C4267 : size_t->int
  # conversion in generated accessors - C4100 : unreferenced parameters in generated helper functions
  foreach(gen_src IN LISTS GEN_FILES)
    set_source_files_properties(${gen_src} APPEND_STRING PROPERTY COMPILE_FLAGS " /wd4267 /wd4100")
  endforeach()
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/serdes/serdes_metrics.cpp" APPEND_STRING PROPERTY
                              COMPILE_FLAGS " /wd4267 /wd4100")
endif()

target_link_libraries(${LIBRARY_NAME} PRIVATE protobuf::libprotobuf)

# find spdlog and use header-only mode. This is slightly slower to compile, but avoids duplicate symbol/linker issues
# from spdlog in multiple translation units.
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${LIBRARY_NAME} PRIVATE spdlog::spdlog_header_only)

set(PUBLIC_HEADERS include/astl/astl.h include/astl/astl_version.h include/astl/astl_errors.h)

# public headers
set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}"
             DEBUG_POSTFIX "d"
             OUTPUT_NAME "astl_static-${PROJECT_VERSION_MAJOR}")

set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES DEBUG_POSTFIX "d" # enable position-independent code, since this is linked into a shared library
             POSITION_INDEPENDENT_CODE ON)
