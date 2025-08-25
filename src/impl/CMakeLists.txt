# build a static library `astl_static` that's easy to link
# with the shared library `astl` or with unit tests.
# useful for providing access to internal functions and classes of the implementation,
# without exporting everything across the shared library interface

set(LIBRARY_NAME "${PROJECT_NAME}_static")
add_library(${LIBRARY_NAME} STATIC)
# add an alias for the library for unambiguous FindPackage
add_library(Astl::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})

include(Warnings)
set_project_warnings(${LIBRARY_NAME})

target_sources(
    ${LIBRARY_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/astl_impl.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/collector/collector_builder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/collector/collector_manager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/collector/scmi_sysfs_collector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/astl_configuration.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/configuration_manager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/topology/topology_builder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/topology/topology_manager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/topology/scmi_topology_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/astl_value.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/metric_manager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/metric_builder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/sampled_value_metric.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/delta_metric.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/rate_metric.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/residency_metric.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/metric/event_metric.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/counter.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/target.cpp"
)

# include directories
target_include_directories(
    ${LIBRARY_NAME}
    PUBLIC
        # where top-level project will look for the library's public headers
        # this static library should make a lot public so unit tests can instantiate objects
        # defined in these directories.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/utils>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# include nlohmann_json - json parser for importing configuration files
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${LIBRARY_NAME} PRIVATE nlohmann_json::nlohmann_json)

# if doing a debug build, include detailed json diagnostics on parse errors
# This setting has two parts:
# - the CMake cache variable JSON_DIAGNOSTICS, which is set in the CmakePresets.json file
#   and used here in the conditional below
# - the preprocessor definition JSON_DIAGNOSTICS which nlohmann::json looks for at compile time.
if (JSON_DIAGNOSTICS)
    target_compile_definitions(${LIBRARY_NAME}
      PRIVATE
        JSON_DIAGNOSTICS=1
    )
endif()


# find spdlog, include it as a header-only dependency. This is a little slower (+10-12s) to compile ASTL,
# but necessary because #including spdlog headers in multiple translation units causes linker errors like LNK2005
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${LIBRARY_NAME} PRIVATE spdlog::spdlog_header_only)

set(public_headers
    include/astl/astl.h
    include/astl/astl_version.h
    include/astl/astl_errors.h
)

# public headers
set_target_properties(
    ${LIBRARY_NAME}
    PROPERTIES
        PUBLIC_HEADER "${public_headers}"
        DEBUG_POSTFIX "d"
        OUTPUT_NAME "astl-${PROJECT_VERSION_MAJOR}"
)

set_target_properties(${LIBRARY_NAME} PROPERTIES
    DEBUG_POSTFIX "d"
    # enable position-independent code, since this is linked into a shared library
    POSITION_INDEPENDENT_CODE ON
)
