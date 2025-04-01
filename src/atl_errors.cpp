#include "atl/atl_errors.h"

// TODO: Build a map for all of the error codes
const char *atlErrorString(atl_error_code error) {
  if (error == ATL_SUCCESS) {
    return "SUCCESS";
  }
  return "UNKOWN";
}
