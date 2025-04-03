#include "astl_impl.h"

#include <iostream>

astl_error_code AstlCollectorImplement::Test() {
  if (test) {
    std::cout << "Test pass" << std::endl;
  } else {
    std::cout << "Test fails" << std::endl;
  }
  return ASTL_SUCCESS;
}
