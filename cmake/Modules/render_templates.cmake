# Expects you to have already done: include(macros)
# And also called: SetAstlVersion()
configure_file("${PROJECT_SOURCE_DIR}/include/astl/astl_version.h.in"
               "${PROJECT_BINARY_DIR}/include/astl/astl_version.h")
