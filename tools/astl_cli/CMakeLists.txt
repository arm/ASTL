cmake_minimum_required(VERSION 3.0...4.0)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Modules/")

find_package(argparse CONFIG REQUIRED)
find_package(tabulate CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

project(ASTL_CLI)
# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

if(NOT TARGET Astl::astl)
  find_package(astl REQUIRED)
endif()

set(TARGET "astl_cli")
add_executable(${TARGET} astl_cli.cpp collect.cpp list_metrics.cpp run_workload.cpp)
target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/include ${TARGET} ${PROJECT_SOURCE_DIR}/utils)
target_link_libraries(${TARGET} PRIVATE argparse::argparse tabulate::tabulate tomlplusplus::tomlplusplus)

set_project_warnings(${TARGET})
target_link_libraries(${TARGET} PRIVATE Astl::astl)
if(SHARED_KERNEL)
  target_compile_definitions(${TARGET} PUBLIC USED_SHARED)
endif()
