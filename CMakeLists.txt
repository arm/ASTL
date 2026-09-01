# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

# Sanitizer dependency triplets must be selected before project() initializes the vcpkg toolchain.
include(AstlSanitizerTriplet)
astl_configure_sanitizer_vcpkg_triplet()

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

# Normalize a CPU architecture string into the artifact directory name used by ASTL.
function(astl_normalize_architecture architecture_name output_variable)
  string(TOLOWER "${architecture_name}" astl_architecture)
  if(astl_architecture MATCHES "^(aarch64|arm64)$")
    set(astl_architecture "arm64")
  elseif(astl_architecture MATCHES "^(amd64|x64|x86_64)$")
    set(astl_architecture "x86_64")
  elseif(astl_architecture MATCHES "^(i[3-6]86|x86)$")
    set(astl_architecture "x86")
  elseif(astl_architecture STREQUAL "")
    set(astl_architecture "unknown")
  else()
    string(REGEX REPLACE "[^A-Za-z0-9_+-]" "_" astl_architecture "${astl_architecture}")
  endif()

  set(${output_variable}
      "${astl_architecture}"
      PARENT_SCOPE)
endfunction()

# Set a build output directory if the caller has not provided a custom path.
function(astl_set_default_output_directory variable_name subdirectory)
  set(astl_default_output_directory "${ASTL_OUTPUT_ROOT}/${subdirectory}")
  set(astl_legacy_output_directory "${PROJECT_BINARY_DIR}/${subdirectory}")
  set(astl_uses_project_default OFF)

  if(NOT DEFINED ${variable_name}
     OR "${${variable_name}}" STREQUAL ""
     OR "${${variable_name}}" STREQUAL "${astl_legacy_output_directory}")
    set(${variable_name}
        "${astl_default_output_directory}"
        CACHE PATH "" FORCE)
    set(astl_uses_project_default ON)
  elseif("${${variable_name}}" STREQUAL "${astl_default_output_directory}")
    set(astl_uses_project_default ON)
  endif()

  foreach(astl_configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(astl_configuration_variable "${variable_name}_${astl_configuration}")
    if(astl_uses_project_default
       AND (NOT DEFINED ${astl_configuration_variable}
            OR "${${astl_configuration_variable}}" STREQUAL ""
            OR "${${astl_configuration_variable}}" STREQUAL "${astl_legacy_output_directory}"))
      set(${astl_configuration_variable}
          "${astl_default_output_directory}"
          CACHE PATH "" FORCE)
    elseif(DEFINED ${astl_configuration_variable} AND "${${astl_configuration_variable}}" STREQUAL
                                                      "${astl_legacy_output_directory}")
      set(${astl_configuration_variable}
          "${${variable_name}}"
          CACHE PATH "" FORCE)
    endif()
  endforeach()
endfunction()

# Add a subdirectory only when the source tree is present. This keeps partial checkouts or intentionally removed
# optional tools buildable.
function(astl_add_optional_subdirectory relative_dir component_name)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${relative_dir}/CMakeLists.txt")
    add_subdirectory(${relative_dir})
  else()
    message(STATUS "Skipping ${component_name} because ${relative_dir} is missing")
  endif()
endfunction()

if(PROJECT_IS_TOP_LEVEL)
  message(STATUS "This project is a top-level project")
  astl_normalize_architecture("${CMAKE_HOST_SYSTEM_PROCESSOR}" ASTL_DETECTED_HOST_ARCHITECTURE)
  set(ASTL_OUTPUT_ARCHITECTURE
      "${ASTL_DETECTED_HOST_ARCHITECTURE}"
      CACHE STRING "Architecture directory name used for build artifacts")
  astl_normalize_architecture("${ASTL_OUTPUT_ARCHITECTURE}" ASTL_OUTPUT_ARCHITECTURE)
  set(ASTL_OUTPUT_ARCHITECTURE
      "${ASTL_OUTPUT_ARCHITECTURE}"
      CACHE STRING "Architecture directory name used for build artifacts" FORCE)
  set(ASTL_OUTPUT_ROOT "${PROJECT_BINARY_DIR}/${ASTL_OUTPUT_ARCHITECTURE}")
  message(STATUS "ASTL output architecture: ${ASTL_OUTPUT_ARCHITECTURE} (host: ${CMAKE_HOST_SYSTEM_PROCESSOR})")
  message(STATUS "ASTL output root: ${ASTL_OUTPUT_ROOT}")
  # Put executables in bin directory
  astl_set_default_output_directory(CMAKE_RUNTIME_OUTPUT_DIRECTORY bin)
  # Put libraries in lib directory
  astl_set_default_output_directory(CMAKE_LIBRARY_OUTPUT_DIRECTORY lib)
  astl_set_default_output_directory(CMAKE_ARCHIVE_OUTPUT_DIRECTORY lib)
else()
  message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
endif()

# Ensure executables and shared libraries can find .so dependencies relative to their own location at runtime. $ORIGIN
# resolves to the directory containing the binary.
if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
  set(CMAKE_BUILD_RPATH "$ORIGIN;$ORIGIN/../lib")
  # Avoid a separate install-time relink step, which some Ninja/CMake environments reject.
  set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

# Add ASTL_DEBUG macro for all targets
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(ASTL_DEBUG)
endif()

set(CMAKE_CXX_STANDARD 23)

if(MSVC)
  # Enable UTF-8 file encoding and standard C++ exception handling (no SEH). /wd4267: suppress size_t->int conversion
  # noise from generated protobuf headers.
  add_compile_options(/utf-8 /EHsc /wd4267)
endif()

message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# Need exact match with Clang to avoid matching "AppleClang"
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Workaround for std::expected not available in clang 18 and earlier, use gcc's libstdc++
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libstdc++> $<$<COMPILE_LANGUAGE:CXX>:-D__cpp_concepts=202002>
                      $<$<COMPILE_LANGUAGE:CXX>:-Wno-builtin-macro-redefined>)

  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 18)
    # add -fclang-abi-compat=17 to fix absl compile errors with clang 18+ (see links below) -
    # https://github.com/abseil/abseil-cpp/issues/1747 - https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=280885
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fclang-abi-compat=17>)
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

include(detect_libc)
astl_detect_libc()

option(ASTL_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(ASTL_BUILD_TOOLS "Build tools" ${PROJECT_IS_TOP_LEVEL})
option(ASTL_BUILD_SAMPLES "Build samples with library" ${PROJECT_IS_TOP_LEVEL})
option(ASTL_BUILD_SHARED "Build the ASTL shared-library wrapper" ON)
option(ATX_STATIC_LINK "Also build ATX as a fully static Linux executable" OFF)
# option(ASTL_INSTALL "Install library when invoking --install" ON) option(ASTL_ENABLE_LOGGING "Enable logging" OFF)

# Enable code coverage - typically on debug builds
option(ENABLE_VALGRIND "Enable run-time memory checking on unit tests" OFF)
option(ASTL_VALGRIND_TRACK_ORIGINS "Track origins of uninitialized values in Valgrind runs" OFF)
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

include(Sanitizers)
astl_configure_sanitizers()

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

# Configure optional procfs support. Unlike libsensors, procfs has no external dependency to probe, so a simple ON/OFF
# option is sufficient.
option(ASTL_PROCFS "Build procfs collector and metric-discovery support" ON)
if(ASTL_PROCFS)
  set(ASTL_INCLUDE_PROCFS ON)
  message(STATUS "procfs support enabled")
else()
  set(ASTL_INCLUDE_PROCFS OFF)
  message(STATUS "procfs support explicitly disabled (ASTL_PROCFS=OFF)")
endif()

# Configure optional libsensors support.
set(ASTL_LIBSENSORS
    "AUTO"
    CACHE STRING "libsensors support: AUTO, ON, or OFF")
set_property(CACHE ASTL_LIBSENSORS PROPERTY STRINGS AUTO ON OFF)

string(TOUPPER "${ASTL_LIBSENSORS}" ASTL_LIBSENSORS)
if(NOT ASTL_LIBSENSORS STREQUAL "AUTO"
   AND NOT ASTL_LIBSENSORS STREQUAL "ON"
   AND NOT ASTL_LIBSENSORS STREQUAL "OFF")
  message(FATAL_ERROR "ASTL_LIBSENSORS must be one of: AUTO, ON, OFF")
endif()

if(ATX_STATIC_LINK)
  if(NOT ASTL_LIBSENSORS STREQUAL "OFF")
    message(STATUS "ATX_STATIC_LINK=ON requires ASTL_LIBSENSORS=OFF; disabling libsensors support")
  endif()
  set(ASTL_LIBSENSORS "OFF")
  set(ASTL_LIBSENSORS
      "OFF"
      CACHE STRING "libsensors support: AUTO, ON, or OFF" FORCE)
endif()

set(ASTL_HAVE_LIBSENSORS OFF)
set(ASTL_INCLUDE_LIBSENSORS OFF)
unset(ASTL_SENSORS_LIB CACHE)

if(ASTL_LIBSENSORS STREQUAL "OFF")
  message(STATUS "libsensors support explicitly disabled (ASTL_LIBSENSORS=OFF)")
else()
  find_library(ASTL_SENSORS_LIB sensors)
  if(ASTL_SENSORS_LIB)
    set(ASTL_HAVE_LIBSENSORS ON)
    set(ASTL_INCLUDE_LIBSENSORS ON)
    message(STATUS "libsensors support enabled")
  elseif(ASTL_LIBSENSORS STREQUAL "ON")
    message(FATAL_ERROR "ASTL_LIBSENSORS=ON but libsensors was not found. Please install libsensors-dev package")
  else()
    message(STATUS "libsensors not found: building without libsensors support")
  endif()
endif()

# define the static library `astl_static` - aka Astl::astl_static
add_subdirectory(src/impl)

if(ASTL_BUILD_SHARED)
  include(add_shared_library)
  astl_add_shared_library()
else()
  message(STATUS "ASTL shared-library wrapper disabled (ASTL_BUILD_SHARED=OFF)")
endif()

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

    set(ASTL_VALGRIND_OPTIONS "--leak-check=full" "--show-leak-kinds=definite,possible"
                              "--errors-for-leak-kinds=definite,possible")
    if(ASTL_VALGRIND_TRACK_ORIGINS)
      list(APPEND ASTL_VALGRIND_OPTIONS "--track-origins=yes")
    endif()
    list(JOIN ASTL_VALGRIND_OPTIONS " " ASTL_VALGRIND_OPTIONS)
    set(VALGRIND_COMMAND "${VALGRIND_PATH}")
    set(VALGRIND_COMMAND_OPTIONS "${ASTL_VALGRIND_OPTIONS}")
    set(MEMORYCHECK_COMMAND "${VALGRIND_PATH}")
    set(MEMORYCHECK_COMMAND_OPTIONS "${ASTL_VALGRIND_OPTIONS}")
  endif()
endif()

if(UNIX AND NOT APPLE)
  if(ASTL_BUILD_TOOLS)
    astl_add_optional_subdirectory(tools/mock_scmi mock_scmi)
    astl_add_optional_subdirectory(tools/scmi_bridge scmi_bridge)
  else()
    message(STATUS "Skipping MockScmi and scmi_bridge because ASTL_BUILD_TOOLS is disabled")
  endif()
else()
  message(STATUS "Skipping MockScmi and scmi_bridge on this platform")
endif()

# define a CLI application to use the library
if(ASTL_BUILD_TOOLS)
  astl_add_optional_subdirectory(tools/ATX ATX)
  astl_add_optional_subdirectory(tools/AMX AMX)
  astl_add_optional_subdirectory(tools/turbostat-arm turbostat-arm)
endif()

include(CTest)

# add samples
if(BUILD_TESTING
   AND ASTL_BUILD_SAMPLES
   AND ASTL_BUILD_SHARED)
  add_subdirectory(samples/sample_test)
elseif(BUILD_TESTING AND ASTL_BUILD_SAMPLES)
  message(STATUS "Skipping samples because ASTL_BUILD_SHARED is disabled")
endif()

# add tests
if(BUILD_TESTING AND ASTL_BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests/unit_test)
  if(ASTL_BUILD_TOOLS)
    astl_add_optional_subdirectory(tools/ATX/tests "ATX tests")
    if(TARGET atx_unit_test)
      include(AstlValgrindShards)
      astl_add_valgrind_shards(
        atx_unit_test
        SHARD_COUNT
        1
        TEST_SPEC
        "~[time_sensitive]~[valgrind_isolated]"
        ENVIRONMENT
        "ASTL_CONFIG_DIR=${CMAKE_SOURCE_DIR}/tests/config;DEBUGINFOD_URLS=")
    endif()
    astl_add_optional_subdirectory(tools/AMX/tests "AMX tests")
  endif()
  if(ASTL_BUILD_SHARED)
    add_subdirectory(tests/wrapper_test)
    add_subdirectory(tests/e2e_test)
  else()
    message(STATUS "Skipping shared-library wrapper and end-to-end tests because ASTL_BUILD_SHARED is disabled")
  endif()
  if(ASTL_HAVE_LIBSENSORS)
    add_subdirectory(tests/libsensors_collector_test)
  else()
    message(STATUS "Skipping libsensors_collector_test because libsensors support is disabled")
  endif()
endif()

if(ASTL_BUILD_SHARED)
  export(
    TARGETS ${PROJECT_NAME}
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/AstlTargets.cmake"
    NAMESPACE Astl::)
endif()

if(ASTL_INSTALL AND ASTL_BUILD_SHARED)
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

elseif(ASTL_INSTALL)
  message(STATUS "Skipping shared-library package installation because ASTL_BUILD_SHARED is disabled")
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
  COMMENT "Running sharded unit tests with Valgrind (runtime memory leak checker)")

# To generate pretty class diagrams and function call graphs with doxygen, run this target after configuring: cmake
# --build build/debug/ --target doxygen firefox html/index.html
add_custom_target(
  doxygen
  COMMAND doxygen ${CMAKE_SOURCE_DIR}/doxyfile
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Render class diagrams and function call graphs with doxygen.  Typically outputs to html/index.html")
