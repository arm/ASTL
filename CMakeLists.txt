set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
                      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")

cmake_minimum_required(VERSION 3.0...4.0) # setting this is required

include(macros)
SetAtlVersion()

project(
  atl
  VERSION ${ATL_VERSION}
  DESCRIPTION "Arm Telemetry Library"
  HOMEPAGE_URL "https://github.com/Arm-Debug/ATL"
  LANGUAGES C CXX
)

message("version is set to: ${ATL_VERSION} major: ${PROJECT_VERSION_MAJOR} minor: ${PROJECT_VERSION_MINOR} micro: ${PROJECT_VERSION_PATCH}")

if (PROJECT_IS_TOP_LEVEL)
  message(STATUS "This project is a top-level project")
  # Put executables in bin directory
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin CACHE STRING "")
  # Put libraries in lib directory
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib CACHE STRING "")
else()
  message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET "hidden")

set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(ATL_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(ATL_BUILD_SAMPLES "Build samples with library" ${PROJECT_IS_TOP_LEVEL})
#option(ATL_INSTALL "Install library when invoking --install" ON)
#option(ATL_ENABLE_LOGGING "Enable logging" OFF)

include(GNUInstallDirs)
set(ATL_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/atl")
set(ATL_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}")
set(ATL_INSTALL_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
set(ATL_INSTALL_BIN_DIR "${CMAKE_INSTALL_BINDIR}")
set(ATL_INSTALL_LIC_DIR "${CMAKE_INSTALL_DOCDIR}")
set(ATL_API_SOVERSION "${PROJECT_VERSION_MAJOR}")

# Target library

configure_file(${PROJECT_SOURCE_DIR}/include/atl/atl_version.h.in
               ${PROJECT_BINARY_DIR}/include/atl/atl_version.h)

add_library(${PROJECT_NAME} SHARED)

# ATL_API macro will export functions on dlls, rather than import with ATL_BUILD set
target_compile_definitions(${PROJECT_NAME} PRIVATE ATL_BUILD)

target_sources(
    ${PROJECT_NAME}
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/atl.cpp"
        "${PROJECT_SOURCE_DIR}/src/atl_version.cpp"
        "${PROJECT_SOURCE_DIR}/src/atl_errors.cpp"
        "${PROJECT_SOURCE_DIR}/src/atl_impl.cpp"
)

add_library(Atl::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# include directories
target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        # where the library itself will look for its internal headers
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC
        # where top-level project will look for the library's public headers
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set(public_headers
    include/atl/atl.h
    include/atl/atl_version.h
    include/atl/atl_errors.h
)

# public headers
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    PUBLIC_HEADER "${public_headers}"
    DEBUG_POSTFIX "d"
    OUTPUT_NAME "atl-${PROJECT_VERSION_MAJOR}"
)

# Debug suffix
set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")

# Definition of CMAKE_INSTALL_LIBDIR, etc
include(GNUInstallDirs)

# install the target
install(TARGETS ${PROJECT_NAME}
    EXPORT "${PROJECT_NAME}Targets"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# add samples
if(BUILD_TESTING AND ATL_BUILD_SAMPLES)
 add_subdirectory(samples/sample_test)
endif()

# add tests
if(BUILD_TESTING AND ATL_BUILD_TESTIN)
 add_subdirectory(test)
endif()

export(
  TARGETS ${PROJECT_NAME}
  FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/AtlTargets.cmake"
  NAMESPACE Atl::)

if(ATL_INSTALL)
  include(CMakePackageConfigHelpers)

  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/AtlConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion)

  install(
    FILES "${PROJECT_SOURCE_DIR}/AtlConfig.cmake"
          "${PROJECT_BINARY_DIR}/AtlConfigVersion.cmake"
    COMPONENT Atl_Development
    DESTINATION "${ATL_INSTALL_CMAKE_DIR}")

  install(
    TARGETS atl
    EXPORT AtlTargets
    LIBRARY DESTINATION ${ATL_INSTALL_LIB_DIR}
            COMPONENT Atl_Runtime
            NAMELINK_COMPONENT Atl_Development
    ARCHIVE DESTINATION ${ATL_INSTALL_LIB_DIR} COMPONENT Atl_Development
    RUNTIME DESTINATION ${ATL_INSTALL_BIN_DIR} COMPONENT Atl_Runtime)

  install(
    DIRECTORY "${PROJECT_BINARY_DIR}/include/atl"
    DESTINATION "${ATL_INSTALL_INCLUDE_DIR}"
    COMPONENT Atl_Development
    FILES_MATCHING
    PATTERN "*.h")

  install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION "${ATL_INSTALL_INCLUDE_DIR}"
    COMPONENT Atl_Development
    FILES_MATCHING
    PATTERN "*.h")

  install(
    EXPORT AtlTargets
    FILE AtlTargets.cmake
    NAMESPACE Atl::
    DESTINATION "${ATL_INSTALL_CMAKE_DIR}"
    COMPONENT Atl_Development)

endif()

#if(PROJECT_IS_TOP_LEVEL AND ATL_INSTALL)
#  include(cmake/packaging.cmake)
#endif()
