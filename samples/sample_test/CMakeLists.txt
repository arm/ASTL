set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
                      "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Modules/")

cmake_minimum_required(VERSION 3.0...4.0)


project (SAMPLE_TEST)

if (NOT CMAKE_BUILD_TYPE)
    message (STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
    set (CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE
        STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel"
        FORCE)
endif()

include(CheckCXXCompilerFlag)

if (NOT TARGET Atl::atl)
  find_package(atl REQUIRED)
endif()

add_subdirectory (src)
