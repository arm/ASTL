#include <iostream>

#include "atl_impl.h"

atl_error_code AtlCollectorImplement::Test() {
  if (test) {
    std::cout << "Test pass" << std::endl;
  } else {
    std::cout << "Test fails" << std::endl;
  }
  return ATL_SUCCESS;
}
