cmake_minimum_required(VERSION 3.0...4.0)

project (UNIT_TEST)

if (NOT CMAKE_BUILD_TYPE)
    message (STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
    set (CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE
        STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel"
        FORCE)
endif()

include(CheckCXXCompilerFlag)

if (NOT TARGET Astl::astl_static)
  find_package(astl_static REQUIRED)
endif()

add_subdirectory (src)

# ctest will add the unit_test executable to its list of tests
# to use this, after building the project, `cd build && ctest`
# PRs should trigger ctest as well from the `integration.yml` workflow
add_test(NAME UnitTest COMMAND ${CMAKE_BINARY_DIR}/bin/unit_test)