cmake_minimum_required(VERSION 3.13) # target link directories support
project(MockSysfs LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

file(GLOB_RECURSE MOCK_SYSFS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(MockSysfs ${MOCK_SYSFS_SOURCES})

include (CTest)
enable_testing()

# TODO(#77) re-enable these tests once issue 77 is resolved
#add_test(NAME toolsMocksysfsFind
#      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/unit_test_fixture.sh
#              $<TARGET_FILE:MockSysfs>
#              ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/find/expected.txt
#              ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/find/test.sh
#)

#add_test(NAME toolsMocksysfsCdPermissions
#      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/unit_test_fixture.sh
#              $<TARGET_FILE:MockSysfs>
#              ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/cd_permissions/expected.txt
#              ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/cd_permissions/test.sh
#)

#add_test(NAME toolsMocksysfsAllDesEnableToIndividualEnableConsistency
#      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/unit_test_fixture.sh
#              $<TARGET_FILE:MockSysfs>
#              ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/all_des_enable_to_individual_enable_consistency/expected.txt
#              ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/all_des_enable_to_individual_enable_consistency/test.sh
#)

target_compile_definitions(MockSysfs
  PRIVATE
    FUSE_USE_VERSION=316  # make sure to update scripts/lint.sh if this changes
)

target_include_directories(MockSysfs
  PRIVATE
    ${FUSE3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_directories(MockSysfs
  PRIVATE
    ${FUSE3_LIBRARY_DIRS}
)

target_link_libraries(MockSysfs
  PRIVATE
    ${FUSE3_LIBRARIES}
)
