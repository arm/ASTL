cmake_minimum_required(VERSION 3.13) # target link directories support

project(MockSysfs VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror -g)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE _FILE_OFFSET_BITS=64)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${FUSE3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${FUSE3_LIBRARY_DIRS}
)

target_link_libraries(${PROJECT_NAME} ${FUSE3_LIBRARIES})
