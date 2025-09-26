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

set(SRCS
    libsensors_collector_utests.cpp
    libsensors_topology_plugin_utests.cpp
    mock_libsensors.cpp
)
set(TARGET "libsensors_test")

# find libsensors, just for the header
find_library(SENSORS_LIB sensors)
if (SENSORS_LIB)
    MESSAGE(STATUS "libsensors found, building libsensors_collector_utests")
    add_executable(${TARGET} ${SRCS})
    # note - do _not_ link to the real libsensors here, since we're mocking it out with mock_libsensors.cpp
    target_compile_definitions(${TARGET} PRIVATE ASTL_HAVE_SENSORS)

    include(Warnings)
    set_project_warnings(${TARGET})

    # astl.h and others should come as include paths along with the taret_link_libraries joining Astl::astl_static
    target_link_libraries(${TARGET}
        # test targets should link astl publicly so that gcovr can measure how much of astl_static is covered
        PUBLIC
            Astl::astl
            Astl::astl_static
        PRIVATE
            Catch2::Catch2WithMain
    )

    # have catch2's cmake module discover the tests and add them to ctest for us
    include(CTest)
    include(Catch)
    catch_discover_tests(${TARGET} ADD_TAGS_AS_LABELS)
else()
    message(STATUS "libsensors not found: please install libsensors-dev package. sensors_collector_utests will not be built without sensors.h")
endif()

