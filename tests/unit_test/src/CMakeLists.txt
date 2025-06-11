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

add_executable(unit_test
    collector_manager_utests.cpp
    file_interface_utests.cpp
    logger_utests.cpp
    scmi_sysfs_collector_utests.cpp
    unit_test.cpp
    metric_utests.cpp
 )

# astl.h and others should come as include paths along with the taret_link_libraries joining Astl::astl_static
target_link_libraries(unit_test
    # test targets should link astl publicly so that gcovr can measure how much of astl_static is covered
    PUBLIC
        Astl::astl
        Astl::astl_static
    PRIVATE
        Catch2::Catch2WithMain
)
add_custom_target(rununittests unit_test)


# have catch2's cmake module discover the tests and add them to ctest for us
include(CTest)
include(Catch)
catch_discover_tests(unit_test)
