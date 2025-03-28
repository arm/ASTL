#ifndef INCLUDE_ATL_ERRORS_H
#define INCLUDE_ATL_ERRORS_H

#include "atl/atl_utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum {
  ATL_SUCCESS = 0,
} atl_error_code;

ATL_EXPORT const char * atlErrorString(atl_error_code error);

#if defined(__cplusplus)
}
#endif

#endif // INCLUDE_ATL_ERRORS_H
