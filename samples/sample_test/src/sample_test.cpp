#include <iostream>

#include "atl/atl.h"

int main(int argc, char *argv[]) {

  atl_version version = atlVersion();

  std::cout << "ATL version Major: " << version._major
            << " Minor: " << version._minor << " Micro: " << version._micro
            << std::endl;

  std::cout << "ATL version is: " << atlVersionString() << std::endl;

  atl_error_code status = atlTest();

  std::cout << "error code is: " << atlErrorString(status) << std::endl;

  return 0;
}
