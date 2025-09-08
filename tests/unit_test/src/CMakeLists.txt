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

set(UNIT_TEST_SRCS
    astl_value_utests.cpp
    collector_manager_utests.cpp
    file_interface_utests.cpp
    logger_utests.cpp
    orchestrator_utests.cpp
    scmi_sysfs_collector_utests.cpp
    sampled_value_metric_utests.cpp
    delta_metric_utests.cpp
    rate_metric_utests.cpp
    residency_metric_utests.cpp
    event_metric_utests.cpp
    config_manager_utests.cpp
    metric_manager_utests.cpp
    scmi_specification_json_utests.cpp
)

find_library(SENSORS_LIB sensors)
if (SENSORS_LIB)
    MESSAGE(STATUS "libsensors found, building sensors_collector_utests")
else()
    message(STATUS "libsensors not found: please install libsensors-dev package. sensors_collector_utests will not be built")
endif()

add_executable(unit_test ${UNIT_TEST_SRCS})

if (SENSORS_LIB)
    target_link_libraries(unit_test PRIVATE ${SENSORS_LIB})
    target_compile_definitions(unit_test PRIVATE ASTL_HAVE_SENSORS)
endif()


include(Warnings)
set_project_warnings(unit_test)

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
catch_discover_tests(unit_test
    ADD_TAGS_AS_LABELS)
