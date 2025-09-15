cmake_minimum_required(VERSION 3.0...4.0)

find_package(Catch2 CONFIG REQUIRED)

project (WRAPPER_TEST)

if (NOT CMAKE_BUILD_TYPE)
    message (STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
    set (CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE
        STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel"
        FORCE)
endif()

if (NOT TARGET Astl::astl)
  find_package(astl REQUIRED)
endif()

add_subdirectory (src)
