#ifndef TEST_INCLUDES_HPP_
#define TEST_INCLUDES_HPP_

// This file #includes all necessary headers from test dependencies, such as catch2 and trompeloeil.
// importantly, it defines some StringMaker specializations for custom types used in the tests,
// which _must_ be defined before including some catch2 headers.

#include <catch2/catch_tostring.hpp>
#include <magic_enum/magic_enum.hpp>
#include <string>

#include "astl/astl_errors.h"

/**
 * @brief Extend catch2's StringMaker to support astl_status_code. Include this header before including catch2.
 *
 */
namespace Catch {
template <>
struct StringMaker<astl_status_code> {
  static std::string convert(astl_status_code value) { return std::string(magic_enum::enum_name(value)); }
};

}  // namespace Catch

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/trompeloeil.hpp>

#endif  // TEST_INCLUDES_HPP_
