#include <iostream>

#include "astl/astl.h"

int main(int argc, char *argv[]) {
  astl_version_t version = astlVersion();

  std::cout << "ASTL version Major: " << version._major << " Minor: " << version._minor << " Micro: " << version._micro
            << std::endl;

  std::cout << "ASTL version is: " << astlVersionString() << std::endl;

  astl_error_code status = astlTest();

  std::cout << "error code is: " << astlErrorString(status) << std::endl;

  return 0;
}
