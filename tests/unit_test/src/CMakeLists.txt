# set the default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
    message(
        STATUS "Default CMAKE_BUILD_TYPE not set. Using Release with Debug Info"
    )
    set(CMAKE_BUILD_TYPE
        "RelWithDebInfo"
        CACHE
          STRING
          "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel"
          FORCE
    )
endif()

add_executable(unit_test unit_test.cpp)

# atl.h and others should come as include paths along with the taret_link_libraries joining Astl::astl_static
target_link_libraries(unit_test
    PUBLIC
        Astl::astl_static
    PRIVATE
        Catch2::Catch2WithMain
)
add_custom_target(rununittests unit_test)

# have catch2's cmake module discover the tests and add them to ctest for us
include(CTest)
include(Catch)
catch_discover_tests(unit_test)
