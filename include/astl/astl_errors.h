#ifndef INCLUDE_ASTL_ERRORS_H_
#define INCLUDE_ASTL_ERRORS_H_

#include "astl/astl_utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum {
  ASTL_SUCCESS = 0,
} astl_error_code;

/**
 * @brief Returns the string version of the Arm SoC Telemetry Library error code
 *
 * @return c-string of astl error code
 */
ASTL_API const char* astlErrorString(astl_error_code error);

#if defined(__cplusplus)
}
#endif

#endif  // INCLUDE_ASTL_ERRORS_H_
