#include "astl_impl.h"

#include <iostream>

namespace astl {

astl_error_code CollectorImplement::Test() {
  std::cout << "Test method is deprecated" << std::endl;
  return ASTL_ERROR_DEPRECATED_API;
}

}  // namespace astl
