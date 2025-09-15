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

if (NOT TARGET Astl::astl)
  find_package(astl REQUIRED)
endif()

add_subdirectory (src)

# ctest will add the sample_test executable to its list of tests
# to use this, after building the project, `cd build && ctest`
# PRs should trigger this as well from the `integration.yml` workflow
add_test(
  NAME SampleTest
  COMMAND ${CMAKE_BINARY_DIR}/bin/sample_test
)
add_test(
  NAME SampleTest_Immediate
  COMMAND ${CMAKE_BINARY_DIR}/bin/sample_test --immediate
)
add_test(
  NAME SampleTest_Interval
  COMMAND ${CMAKE_BINARY_DIR}/bin/sample_test --interval=50
)
set_tests_properties(SampleTest PROPERTIES LABELS "integration;sample")
