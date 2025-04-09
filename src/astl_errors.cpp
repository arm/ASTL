#include "astl/astl.h"

// TODO(https://github.com/Arm-Debug/ASTL/issues/16): Build a map for all of the error codes
const char* astlErrorString(astl_error_code error) {
  if (error == ASTL_SUCCESS) {
    return "SUCCESS";
  }
  return "UNKOWN";
}
