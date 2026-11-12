# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Check the selected target compiler and library without executing target code when cross-compiling.
function(astl_check_cxx_features)
  find_package(tl-expected CONFIG REQUIRED)
  find_package(tl-optional CONFIG REQUIRED)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_INCLUDES "${PROJECT_SOURCE_DIR}/utils")
  set(CMAKE_REQUIRED_LIBRARIES tl::expected tl::optional)
  # A standard change must recheck the selected implementations in an existing build directory.
  unset(ASTL_HAVE_REQUIRED_CXX_FEATURES CACHE)
  check_cxx_source_compiles(
    [[
      #include "astl_cxx_compat.hpp"
      #include <format>
      #include <string>

      int main() {
        astl::compat::expected<int, int> value{42};
        astl::compat::expected<void, int> status{};
        auto transformed = astl::compat::optional<int>{*value}.transform([](int n) { return n + 1; });
        auto formatted = std::format("{}", transformed.value());
        return !status || formatted.empty();
      }
    ]]
    ASTL_HAVE_REQUIRED_CXX_FEATURES)

  if(NOT ASTL_HAVE_REQUIRED_CXX_FEATURES)
    message(
      FATAL_ERROR
        "ASTL requires C++20 or newer with std::format and compatible expected/optional implementations. "
        "Provide tl-expected and tl-optional for standard-library fallbacks. "
        "Select a compatible toolchain and rebuild its C++ dependencies. "
        "For the Android NDK, select libc++ with ANDROID_STL=c++_static or ANDROID_STL=c++_shared.")
  endif()
endfunction()
