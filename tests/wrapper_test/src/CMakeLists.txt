# set the default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Default CMAKE_BUILD_TYPE not set. Using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

add_executable(wrapper_test wrapper_test.cpp metric_groups_tests.cpp)

target_compile_definitions(wrapper_test PRIVATE ASTL_BUILD)

include(Warnings)
set_project_warnings(wrapper_test)

# astl.h and others should come as include paths along with the target_link_libraries joining Astl::astl_static
target_link_libraries(
  wrapper_test
  # test targets should link astl publicly so that gcovr can measure how much of astl_static is covered
  PUBLIC Astl::astl_static
  PRIVATE Catch2::Catch2WithMain)
add_custom_target(
  runwrappertests
  COMMAND wrapper_test
  COMMENT "Run wrapper test")

# have catch2's cmake module discover the tests and add them to ctest for us
include(CTest)
include(Catch)

# tests will be run with ASTL_CONFIG_DIR set to the config directory in the build tree
catch_discover_tests(wrapper_test ADD_TAGS_AS_LABELS PROPERTIES ENVIRONMENT
                     "ASTL_CONFIG_DIR=${CMAKE_SOURCE_DIR}/tests/config")
