#include <catch2/catch_test_macros.hpp>

#include "astl/astl.h"
#include "astl_impl.hpp"

TEST_CASE("Orchestrator.Test()", "[is deprecated]") {
  REQUIRE(astl::Orchestrator::GetInstance()->Test() == ASTL_STATUS_DEPRECATED_API);
}
