
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


# Find libsensors
find_library(SENSORS_LIB sensors)
if (SENSORS_LIB)
  set(TARGET "libsensors_example")
  add_executable(${TARGET} main.cpp)
  target_link_libraries(${TARGET} PRIVATE ${SENSORS_LIB})
  set_project_warnings(${TARGET})
else()
  message(STATUS "libsensors not found: please install libsensors-dev package")
endif()
