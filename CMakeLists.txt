# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")

cmake_minimum_required(VERSION 3.0...4.0) # setting this is required

# enable CMake's built-in package management, which integrates with vcpkg
include(FetchContent)
# use vcpkg to manage dependencies
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  # Configure vcpkg bootstrap
  set(VCPKG_DIR "${CMAKE_SOURCE_DIR}/external/vcpkg")
  set(VCPKG_BOOTSTRAP_SH "${CMAKE_SOURCE_DIR}/scripts/bootstrap-vcpkg.sh")
  set(VCPKG_BOOTSTRAP_PS1 "${CMAKE_SOURCE_DIR}/scripts/bootstrap-vcpkg.ps1")

  # Only run bootstrap if vcpkg hasn't been built
  if(NOT EXISTS "${VCPKG_DIR}/vcpkg")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
      message(STATUS "Bootstrapping vcpkg (Windows)...")
      execute_process(COMMAND powershell -ExecutionPolicy Bypass -File "${VCPKG_BOOTSTRAP_PS1}"
                      RESULT_VARIABLE _vcpkg_result)
    else()
      message(STATUS "Bootstrapping vcpkg (Unix)...")
      execute_process(COMMAND bash "${VCPKG_BOOTSTRAP_SH}" RESULT_VARIABLE _vcpkg_result)
    endif()

    if(NOT _vcpkg_result EQUAL 0)
      message(FATAL_ERROR "vcpkg bootstrap failed!")
    endif()
  endif()

  # Tell CMake to use vcpkg toolchain
  set(CMAKE_TOOLCHAIN_FILE
      "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

include(macros)
setastlversion()

project(
  astl
  VERSION ${ASTL_VERSION}
  DESCRIPTION "Arm SoC Telemetry Library"
  HOMEPAGE_URL "https://github.com/Arm-Debug/ASTL"
  LANGUAGES C CXX)

message(STATUS "version is set to: ${ASTL_VERSION} " "major: ${PROJECT_VERSION_MAJOR} "
               "minor: ${PROJECT_VERSION_MINOR} " "micro: ${PROJECT_VERSION_PATCH}")

# Sanitizer option (Address + Undefined Behavior)
option(ENABLE_ASAN "Enable Address/Undefined Behavior sanitizer instrumentation" OFF)
option(ASTL_PYTHON_ENABLE_ASAN "Propagate sanitizer flags into Python extension build" OFF)
if(ENABLE_ASAN)
  message(STATUS "Building with sanitizers (ASAN/UBSAN)")
  # IMPORTANT: Pass flags as separate list items so CMake does not quote them together.
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(ASTL_PYTHON_ENABLE_ASAN)
  if(NOT ENABLE_ASAN)
    message(WARNING "ASTL_PYTHON_ENABLE_ASAN is ON but ENABLE_ASAN is OFF; enabling ENABLE_ASAN as well")
    set(ENABLE_ASAN
        ON
        CACHE BOOL "Enable Address/Undefined Behavior sanitizer instrumentation" FORCE)
  endif()
  message(STATUS "Python extension will be built with sanitizer flags (exporting ASTL_PYTHON_ENABLE_ASAN=1)")
  set(ENV{ASTL_PYTHON_ENABLE_ASAN} 1)
endif()

if(PROJECT_IS_TOP_LEVEL)
  message(STATUS "This project is a top-level project")
  # Put executables in bin directory
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/bin
      CACHE STRING "")
  # Put libraries in lib directory
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
      ${PROJECT_BINARY_DIR}/lib
      CACHE STRING "")
else()
  message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
endif()

# Add ASTL_DEBUG macro for all targets
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(ASTL_DEBUG)
endif()

set(CMAKE_CXX_STANDARD 23)

if(MSVC)
  # Enable UTF-8 file encoding and standard C++ exception handling (no SEH)
  add_compile_options(/utf-8 /EHsc)
endif()

message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# Need exact match with Clang to avoid matching "AppleClang"
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Workaround for std::expected not available in clang 18 and earlier, use gcc's libstdc++
  add_compile_options(-stdlib=libstdc++ -D__cpp_concepts=202002 -Wno-builtin-macro-redefined)

  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 18)
    # add -fclang-abi-compat=17 to fix absl compile errors with clang 18+ (see links below) -
    # https://github.com/abseil/abseil-cpp/issues/1747 - https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=280885
    add_compile_options(-fclang-abi-compat=17)
  endif()

endif()

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
# option(ASTL_INSTALL "Install library when invoking --install" ON) option(ASTL_ENABLE_LOGGING "Enable logging" OFF)

# Enable code coverage - typically on debug builds
option(ENABLE_VALGRIND "Enable run-time memory checking on unit tests" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Building with coverage instrumentation")
    add_compile_options("--coverage")
    # Uniquify the names of our .gcda files with the process ID. Ensures parallel tests don't clobber eachother's
    # coverage files.
    add_compile_options("-fprofile-dir=coverage_%p")
    add_compile_options("-O0") # Needed to get reliable coverage results
    add_link_options("--coverage")
  endif()
endif()

include(GNUInstallDirs)
set(ASTL_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/astl")
set(ASTL_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}")
set(ASTL_INSTALL_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
set(ASTL_INSTALL_BIN_DIR "${CMAKE_INSTALL_BINDIR}")
set(ASTL_INSTALL_LIC_DIR "${CMAKE_INSTALL_DOCDIR}")
set(ASTL_API_SOVERSION "${PROJECT_VERSION_MAJOR}")

# clang-tidy setup include headers in compilation database - this makes it easy for clang-tidy in scripts/lint.sh to use
# our compile_commands.json to find system headers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Note - We don't set CMAKE_CXX_CLANG_TIDY here because when building with gcc, clang-tidy can't find system headers.
# scripts/lint.sh calls clang-tidy and searches for necessary system headers. it's accessed through the 'lint' build
# target if you want to run it.

include(render_templates)

# Find libsensors
set(ASTL_INCLUDE_LIBSENSORS OFF)
find_library(SENSORS_LIB sensors)
if(SENSORS_LIB)
  set(ASTL_INCLUDE_LIBSENSORS ON)
  add_compile_definitions(ASTL_INCLUDE_LIBSENSORS)
else()
  message(STATUS "libsensors not found: please install libsensors-dev package")
endif()

# define the static library `astl_static` - aka Astl::astl_static
add_subdirectory(src/impl)

# define the shared library `astl`
set(LIBRARY_NAME "${PROJECT_NAME}")

# Define the target for a shared library `astl`, wrapping the static library astl_static
add_library(${LIBRARY_NAME} SHARED)
# add alias target for the library for unambiguous FindPackage
add_library(Astl::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})

# Create a symlink to the configuration JSON next to the built shared library. We do this after the shared library
# target is defined so $<TARGET_FILE_DIR:${LIBRARY_NAME}> is valid. Source file lives in
# samples/sample_configuration/astl_configuration.json; link name should be astl_configuration.json.
add_custom_command(
  TARGET ${LIBRARY_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/samples/sample_configuration/astl_configuration.json
          $<TARGET_FILE_DIR:${LIBRARY_NAME}>/astl_configuration.json
  COMMENT "Creating symlink to astl_configuration.json in shared library output directory"
  VERBATIM)

include(Warnings)
set_project_warnings(${LIBRARY_NAME})

# ASTL_API macro will export functions on dlls, rather than import with ASTL_BUILD set
target_compile_definitions(${LIBRARY_NAME} PRIVATE ASTL_BUILD)

# if we're in a debug build, we want nlohmann::json library to include detailed diagnostics on failure. This
# 'JSON_DIAGNOSTICS' setting has two parts: - the CMake cache variable JSON_DIAGNOSTICS, which is set in the
# CmakePresets.json file and used here in the conditional below - the preprocessor definition JSON_DIAGNOSTICS which
# nlohmann::json looks for at compile time.
if(JSON_DIAGNOSTICS)
  # Apply globally so all subsequent targets (including tests) inherit it
  add_compile_definitions(JSON_DIAGNOSTICS=1)
  # Also ensure the shared lib explicitly has it (redundant but clear)
  target_compile_definitions(${LIBRARY_NAME} PRIVATE JSON_DIAGNOSTICS=1)
endif()

target_sources(
  ${LIBRARY_NAME}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_errors.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_telemetry.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_test_hooks.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/astl_version.cpp")

# include directories
target_include_directories(
  ${LIBRARY_NAME}
  PRIVATE # where the library itself will look for its internal headers
          ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC # where top-level project will look for the library's public headers
         ${PROJECT_SOURCE_DIR}/include
         ${PROJECT_SOURCE_DIR}/utils
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         # where external projects will look for the library's public headers
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# statically link in `astl_static` to the shared library
target_link_libraries(${LIBRARY_NAME} PRIVATE Astl::astl_static)

# find spdlog, include it as a header-only dependency. This is a little slower (+10-12s) to compile ASTL, but necessary
# because #including spdlog headers in multiple translation units causes linker errors like LNK2005
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${LIBRARY_NAME} PRIVATE spdlog::spdlog_header_only)

set(PUBLIC_HEADERS include/astl/astl.h include/astl/astl_errors.h include/astl/astl_telemetry.h
                   include/astl/astl_version.h)

# public headers
set_target_properties(
  ${LIBRARY_NAME}
  PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}"
             DEBUG_POSTFIX "d"
             OUTPUT_NAME "astl-${PROJECT_VERSION_MAJOR}")

# Debug suffix
set_target_properties(${LIBRARY_NAME} PROPERTIES DEBUG_POSTFIX "d")

# Definition of CMAKE_INSTALL_LIBDIR, etc
include(GNUInstallDirs)

# install the target
install(
  TARGETS ${PROJECT_NAME}
  EXPORT "${PROJECT_NAME}Targets"
  PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
  INCLUDES
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(ENABLE_VALGRIND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_program(VALGRIND_PATH valgrind)
  if(NOT VALGRIND_PATH)
    message(WARNING "Valgrind requested (ENABLE_VALGRIND=ON) but not found in PATH; "
                    "disabling memory checking. Install valgrind and re-configure to enable.")
    set(ENABLE_VALGRIND
        OFF
        CACHE BOOL "Enable run-time memory checking on unit tests" FORCE)
  else()
    # Configure CTest memory checking integration.
    set(MEMORYCHECK_TYPE "Valgrind")
    set(VALGRIND_COMMAND "${VALGRIND_PATH}")
    set(VALGRIND_COMMAND_OPTIONS "--leak-check=full --show-leak-kinds=all --track-origins=yes")
    set(MEMORYCHECK_COMMAND "${VALGRIND_PATH}")
    set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --show-leak-kinds=all --track-origins=yes")
  endif()
endif()

if(UNIX AND NOT APPLE)
  add_subdirectory(tools/mock_sysfs)
else()
  message(STATUS "Skipping mock_sysfs on this platform")
endif()

# define a CLI application to use the library
add_subdirectory(tools/astl_cli)

include(CTest)

# add samples
if(BUILD_TESTING AND ASTL_BUILD_SAMPLES)
  add_subdirectory(samples/sample_test)
endif()

# add tests
if(BUILD_TESTING AND ASTL_BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests/unit_test)
  add_subdirectory(tests/wrapper_test)
  add_subdirectory(tests/libsensors_collector_test)
  add_subdirectory(tests/e2e_test)
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
    FILES "${PROJECT_SOURCE_DIR}/AstlConfig.cmake" "${PROJECT_BINARY_DIR}/AstlConfigVersion.cmake"
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

# if(PROJECT_IS_TOP_LEVEL AND ASTL_INSTALL) include(cmake/packaging.cmake) endif()

# add a phony target to format the source files to use: `cd build && cmake --build . --target format` or: `cd build &&
# make format`
add_custom_target(
  format
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/format.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-format on source files")
# add format command to help in `cmake --build . --target help` and don't build it by default
set_target_properties(format PROPERTIES EXCLUDE_FROM_ALL TRUE)

# create a phony target to check formatting to use: `cd build && cmakke --build . --target check_format` or: `cd build
# && make check_format`
add_custom_target(
  check_format
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_format.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Checking formatting of source files - runs on build and fails if unformatted files are found")

# add a phony target to lint code
add_custom_target(
  lint
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/lint.sh ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-lint on source files")

add_custom_target(
  valgrind
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/valgrind.sh ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running unit tests with Valgrind (runtime memory leak checker)")

# To generate pretty class diagrams and function call graphs with doxygen, run this target after configuring: cmake
# --build build/debug/ --target doxygen firefox html/index.html
add_custom_target(
  doxygen
  COMMAND doxygen ${CMAKE_SOURCE_DIR}/doxyfile
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Render class diagrams and function call graphs with doxygen.  Typically outputs to html/index.html")
