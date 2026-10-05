# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Check the selected target compiler and library without executing target code when cross-compiling.
function(astl_check_cxx_features)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles(
    [[
      #include <expected>
      #include <format>
      #include <optional>
      #include <string>

      int main() {
        std::expected<int, int> value{42};
        std::expected<void, int> status{};
        auto transformed = std::optional<int>{*value}.transform([](int n) { return n + 1; });
        auto formatted = std::format("{}", transformed.value());
        return !status || formatted.empty();
      }
    ]]
    ASTL_HAVE_REQUIRED_CXX_FEATURES)

  if(NOT ASTL_HAVE_REQUIRED_CXX_FEATURES)
    message(
      FATAL_ERROR
        "ASTL requires a C++23 compiler and standard library providing std::expected, std::format, "
        "and std::optional::transform. Select a compatible toolchain and rebuild its C++ dependencies. "
        "For the Android NDK, select libc++ with ANDROID_STL=c++_static or ANDROID_STL=c++_shared.")
  endif()
endfunction()
