#include "astl_impl.h"

#include <iostream>

namespace astl {

astl_error_code CollectorImplement::Test() {
  if (test) {
    std::cout << "Test pass" << std::endl;
  } else {
    std::cout << "Test fails" << std::endl;
  }
  return ASTL_SUCCESS;
}

}  // namespace astl
