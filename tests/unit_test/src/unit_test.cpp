#include <catch2/catch_test_macros.hpp>

#include "astl/astl.h"
#include "astl_impl.h"

TEST_CASE("CollectorInstance.Test()", "[always succeeds]") {
  REQUIRE(astl::CollectorInstance().Test() == ASTL_SUCCESS);
}
