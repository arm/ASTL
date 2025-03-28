
# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
  message(
    STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE
        STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel"
        FORCE)
endif()

add_executable(sample_test sample_test.cpp)
target_compile_options(sample_test PUBLIC -O3)
target_link_options(sample_test PUBLIC -O3)
target_include_directories(
  sample_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

message("cmake_current_source_dir: ${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(sample_test PUBLIC Atl::atl)

if(SHARED_KERNEL)
  target_compile_definitions(sample_test PUBLIC USED_SHARED)
endif()

add_custom_target(runsampletest sample_test)
