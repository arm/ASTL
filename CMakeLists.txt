set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
                      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")

cmake_minimum_required(VERSION 3.0...4.0) # setting this is required



# enable CMake's built-in package management, which integrates with vcpkg
include(FetchContent)
# use vcpkg to manage dependencies
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    message(STATUS "📦 Using vcpkg toolchain from $ENV{VCPKG_ROOT}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    message("📦 Found vcpkg git submodule - using it for dependencies\n")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    message(STATUS "Using vcpkg toolchain from ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg")
  else()
    message(FATAL_ERROR "❌ VCPKG_ROOT not set and no toolchain file provided.\n"
                        " For initial setup, please run:\n"
                        " git submodule update --init --recursive\n"
                        " ./vcpkg/bootstrap-vcpkg.sh\n"
                        )
  endif()
endif()

include(macros)
SetAstlVersion()

project(
  astl
  VERSION ${ASTL_VERSION}
  DESCRIPTION "Arm SoC Telemetry Library"
  HOMEPAGE_URL "https://github.com/Arm-Debug/ASTL"
  LANGUAGES C CXX
)

message("version is set to: ${ASTL_VERSION} major: ${PROJECT_VERSION_MAJOR} minor: ${PROJECT_VERSION_MINOR} micro: ${PROJECT_VERSION_PATCH}")

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

option(ASTL_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(ASTL_BUILD_SAMPLES "Build samples with library" ${PROJECT_IS_TOP_LEVEL})
#option(ASTL_INSTALL "Install library when invoking --install" ON)
#option(ASTL_ENABLE_LOGGING "Enable logging" OFF)

include(GNUInstallDirs)
set(ASTL_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/astl")
set(ASTL_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}")
set(ASTL_INSTALL_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
set(ASTL_INSTALL_BIN_DIR "${CMAKE_INSTALL_BINDIR}")
set(ASTL_INSTALL_LIC_DIR "${CMAKE_INSTALL_DOCDIR}")
set(ASTL_API_SOVERSION "${PROJECT_VERSION_MAJOR}")

# clang-tidy setup
# include headers in compilation database - this makes it easy for clang-tidy in scripts/lint.sh to use our compile_commands.json to find system headers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Note - We don't set CMAKE_CXX_CLANG_TIDY here because when building with gcc, clang-tidy can't find system headers.
#        scripts/lint.sh calls clang-tidy and searches for necessary system headers. it's accessed through the 'lint' build target if you want to run it.


# Target library

configure_file(${PROJECT_SOURCE_DIR}/include/astl/astl_version.h.in
               ${PROJECT_BINARY_DIR}/include/astl/astl_version.h)


# define the static library `astl_static` - aka Astl::astl_static
add_subdirectory(src/impl)

# define the shared library `astl`
set(LIBRARY_NAME "${PROJECT_NAME}")

# Define the target for a shared library `astl`, wrapping the static library astl_static
add_library(${LIBRARY_NAME} SHARED)
# add alias target for the library for unambiguous FindPackage
add_library(Astl::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})

# ASTL_API macro will export functions on dlls, rather than import with ASTL_BUILD set
target_compile_definitions(${LIBRARY_NAME} PRIVATE ASTL_BUILD)

target_sources(
    ${LIBRARY_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_telemetry.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_version.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_errors.cpp"
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
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# statically link in `astl_static` to the shared library
target_link_libraries(${LIBRARY_NAME}
    PRIVATE Astl::astl_static
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

# Debug suffix
set_target_properties(${LIBRARY_NAME} PROPERTIES DEBUG_POSTFIX "d")

# Definition of CMAKE_INSTALL_LIBDIR, etc
include(GNUInstallDirs)

# install the target
install(TARGETS ${PROJECT_NAME}
    EXPORT "${PROJECT_NAME}Targets"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

include (CTest)

# add samples
if(BUILD_TESTING AND ASTL_BUILD_SAMPLES)
 add_subdirectory(samples/sample_test)
endif()

# add tests
if(BUILD_TESTING AND ASTL_BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests/unit_test)
endif()

export(
  TARGETS ${PROJECT_NAME}
  FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/AstlTargets.cmake"
  NAMESPACE Astl::)

if(ASTL_INSTALL)
  include(CMakePackageConfigHelpers)

  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/AstlConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion)

  install(
    FILES "${PROJECT_SOURCE_DIR}/AstlConfig.cmake"
          "${PROJECT_BINARY_DIR}/AstlConfigVersion.cmake"
    COMPONENT Astl_Development
    DESTINATION "${ASTL_INSTALL_CMAKE_DIR}")

  install(
    TARGETS astl
    EXPORT AstlTargets
    LIBRARY DESTINATION ${ASTL_INSTALL_LIB_DIR}
            COMPONENT Astl_Runtime
            NAMELINK_COMPONENT Astl_Development
    PUBLIC_HEADER DESTINATION ${ASTL_INSTALL_INCLUDE_DIR} COMPONENT Astl_Development
    ARCHIVE DESTINATION ${ASTL_INSTALL_LIB_DIR} COMPONENT Astl_Development
    RUNTIME DESTINATION ${ASTL_INSTALL_BIN_DIR} COMPONENT Astl_Runtime)

  install(
    DIRECTORY "${PROJECT_BINARY_DIR}/include/astl"
    DESTINATION "${ASTL_INSTALL_INCLUDE_DIR}"
    COMPONENT Astl_Development
    FILES_MATCHING
    PATTERN "*.h")

  install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION "${ASTL_INSTALL_INCLUDE_DIR}"
    COMPONENT Astl_Development
    FILES_MATCHING
    PATTERN "*.h")

  install(
    EXPORT AstlTargets
    FILE AstlTargets.cmake
    NAMESPACE Astl::
    DESTINATION "${ASTL_INSTALL_CMAKE_DIR}"
    COMPONENT Astl_Development)

endif()

#if(PROJECT_IS_TOP_LEVEL AND ASTL_INSTALL)
#  include(cmake/packaging.cmake)
#endif()


# add a phony target to format the source files
# to use: `cd build && cmake --build . --target format`
#     or: `cd build && make format`
add_custom_target(format
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/format.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-format on source files"
)
# add format command to help in `cmake --build . --target help` and don't build it by default
set_target_properties(format PROPERTIES EXCLUDE_FROM_ALL TRUE)

# create a phony target to check formatting
# to use: `cd build && cmakke --build . --target check_format`
#     or: `cd build && make check_format`
add_custom_target(check_format
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_format.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Checking formatting of source files - runs on build and fails if unformatted files are found"
)

# add a phony target to lint code
add_custom_target(lint
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/lint.sh ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-lint on source files"
)