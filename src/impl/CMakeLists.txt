# build a static library `astl_static` that's easy to link
# with the shared library `astl` or with unit tests.
# useful for providing access to internal functions and classes of the implementation,
# without exporting everything across the shared library interface

set(LIBRARY_NAME "${PROJECT_NAME}_static")
add_library(${LIBRARY_NAME} STATIC)
# add an alias for the library for unambiguous FindPackage
add_library(Astl::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})

target_sources(
    ${LIBRARY_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/astl_impl.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/counter.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/target.cpp"
)

# include directories
target_include_directories(
    ${LIBRARY_NAME}
    PRIVATE
        # where the library itself will look for its internal headers
        ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
        # where top-level project will look for the library's public headers
        ${PROJECT_SOURCE_DIR}/include
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

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