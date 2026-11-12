// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_CXX_COMPAT_HPP_
#define ASTL_CXX_COMPAT_HPP_

/*
 * ASTL is a C++23 codebase that also supports building as C++20. This compatibility layer retains that build for
 * toolchains and SDKs whose libraries lack C++23 expected or optional monadic operations. The fallbacks preserve
 * shared call sites, including optional::transform(). These types are internal and are not exposed by the C API.
 *
 * Select implementations by library feature support, since a C++23 compiler may still use an older library.
 * Outside this compatibility implementation, include this header and use astl::compat names instead of directly
 * including <expected> or <optional>. Additional C++23-only headers or APIs need a tested C++20 fallback before
 * they can be used in shared ASTL code.
 *
 * Drop C++20 support only after maintainers raise the supported toolchain baseline and all required toolchains
 * provide the necessary C++23 facilities. Raise the minimum C++ standard, update CMake feature checks, presets,
 * CI and documentation, then remove the tl dependencies from both vcpkg manifests and CMake link requirements,
 * together with the JSON fallback adapter and fallback-specific test branches. The aliases can continue to
 * forward to std types so shared call sites do not need another migration.
 */
#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  include <expected>
namespace astl::compat {
using std::bad_expected_access;
using std::expected;
using std::in_place;
using std::unexpected;
}  // namespace astl::compat
#else
#  include <tl/expected.hpp>
namespace astl::compat {
using tl::bad_expected_access;
using tl::expected;
using tl::in_place;
using tl::unexpected;
}  // namespace astl::compat
#endif

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L
#  include <optional>
namespace astl::compat {
using std::bad_optional_access;
using std::make_optional;
using std::nullopt;
using std::nullopt_t;
using std::optional;
}  // namespace astl::compat
#else
#  include <tl/optional.hpp>
namespace astl::compat {
using tl::bad_optional_access;
using tl::make_optional;
using tl::nullopt;
using tl::nullopt_t;
using tl::optional;
}  // namespace astl::compat
#endif

#endif  // ASTL_CXX_COMPAT_HPP_
