
# This vcpkg-overlays/fmt directory forces an update of the fmt library
# compared to what vcpkg's spdlog uses.

# https://github.com/gabime/spdlog/discussions/3374
#   https://github.com/microsoft/vcpkg/pull/42936
# basically boils down to:
# spdlog (1.15.2) does not seem to be happy with the current 
# fmt version (11.0.2) when using clang 20
# Getting compile errors like this:
# error: call to consteval function 'fmt::basic_format_string<char, const char *, const char *&, int &>::basic_format_string<FMT_COMPILE_STRING, 0>' is not a constant expression

# with this portfile we can force the fmt version to be 11.1.4
# for simplicity we should remove this once https://vcpkg.link/ports/fmt is updated to 11.1.4 or later
# or if spdlog in vcpkg supports std::format


vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO fmtlib/fmt
    REF 11.1.4
    SHA512 573b7de1bd224b7b1b60d44808a843db35d4bc4634f72a9edcb52cf68e99ca66c744fd5d5c97b4336ba70b94abdabac5fc253b245d0d5cd8bbe2a096bf941e39
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DFMT_TEST=OFF
        -DFMT_DOC=OFF
        -DFMT_INSTALL=ON
)

vcpkg_cmake_install()