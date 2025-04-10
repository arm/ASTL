#ifndef INCLUDE_ASTL_ERRORS_H_
#define INCLUDE_ASTL_ERRORS_H_

#include "astl/astl_utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Error codes
 */
typedef enum _astl_error_code {
  ASTL_SUCCESS                                    = 0,         //!< Success
  ASTL_ERROR_BAD_ARGUMENT                         = 1,         //!< Bad argument passed to function
  ASTL_ERROR_BAD_CONFIGURATION                    = 2,         //!< Generic bad configuration error code
  ASTL_ERROR_INVALID_TARGET_HANDLE                = 3,         //!< Invalid target handle used
  ASTL_ERROR_INVALID_COUNTER_HANDLE               = 4,         //!< Invalid counter handle used
  ASTL_ERROR_INVALID_METRIC_HANDLE                = 5,         //!< Invalid metric handle used
  ASTL_ERROR_INVALID_METRIC_GROUP_HANDLE          = 6,         //!< Invalid metric group handle used
  ASTL_ERROR_NOT_IMPLEMENTED                      = 7,         //!< Functionality not implemented yet
  ASTL_ERROR_NOT_SUPPORTED                        = 8,         //!< Unsupported functionality requested
  ASTL_ERROR_DEPRECATED_API                       = 9,         //!< Deprecated API used
  ASTL_ERROR_OLD_TARGET_PROPERTIES_STRUCT_VERSION = 10,        //!< The version of the target properties structure used
                                                               // by client is older
  ASTL_ERROR_NEW_TARGET_PROPERTIES_STRUCT_VERSION = 11,        //!< The version of the target properties structure used
                                                               // by client is newer
  ASTL_ERROR_OLD_COUNTER_PROPERTIES_STRUCT_VERSION = 12,       //!< The version of the counter properties structure
                                                               // used by client is older
  ASTL_ERROR_NEW_COUNTER_PROPERTIES_STRUCT_VERSION = 13,       //!< The version of the counter properties structure
                                                               // used by client is newer
  ASTL_ERROR_OLD_COUNTER_SAMPLE_STRUCT_VERSION = 14,           //!< The version of the counter sample structure used by
                                                               // client is older
  ASTL_ERROR_NEW_COUNTER_SAMPLE_STRUCT_VERSION = 15,           //!< The version of the counter sample structure used by
                                                               // client is newer
  ASTL_ERROR_OLD_METRIC_PROPERTIES_STRUCT_VERSION = 16,        //!< The version of the metric properties structure used
                                                               // by client is older
  ASTL_ERROR_NEW_METRIC_PROPERTIES_STRUCT_VERSION = 17,        //!< The version of the metric properties structure used
                                                               // by client is newer
  ASTL_ERROR_OLD_METRIC_SAMPLE_STRUCT_VERSION = 18,            //!< The version of the metric sample structure used by
                                                               // client is older
  ASTL_ERROR_NEW_METRIC_SAMPLE_STRUCT_VERSION = 19,            //!< The version of the metric sample structure used by
                                                               // client is newer
  ASTL_ERROR_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION = 20,  //!< The version of the metric group properties structure
                                                               // used by client is older
  ASTL_ERROR_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION = 21,  //!< The version of the metric group properties structure
                                                               // used by client is newer
  ASTL_ERROR_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION = 22,    //!< The version of the collection parameters structure
                                                               // used by client is older
  ASTL_ERROR_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION = 23,    //!< The version of the collection parameters structure
                                                               // used by client is newer
  ASTL_ERROR_TARGET_PROPERTIES_BUFFER_TOO_SMALL = 24,          //!< Buffer of target properties passed in by client is
                                                               // too small to hold all targets
  ASTL_ERROR_COUNTER_PROPERTIES_BUFFER_TOO_SMALL = 25,         //!< Buffer of counter properties passed in by client is
                                                               // too small to hold all counters
  ASTL_ERROR_METRIC_PROPERTIES_BUFFER_TOO_SMALL = 26,          //!< Buffer of metric properties passed in by client is
                                                               // too small to hold all metrics
  ASTL_ERROR_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL = 27,  //!< Buffer of metric group properties passed in by client
                                                             // is too small to hold all metric groups
  ASTL_ERROR_COUNTER_SAMPLES_BUFFER_TOO_SMALL = 28,          //!< Buffer of counter samples passed in by client is too
                                                             // small to hold all counter samples
  ASTL_ERROR_METRIC_SAMPLES_BUFFER_TOO_SMALL = 29,           //!< Buffer of metric samples passed in by client is too
                                                             // small to hold all metric samples
  ASTL_ERROR_SAMPLIMG_INTERVAL_TOO_SMALL = 30,               //!< Sampling interval specified is too small
  ASTL_ERROR_SAMPLING_INTERVAL_TOO_LARGE = 31,               //!< Sampling interval specified is too large
  ASTL_ERROR_SAMPLING_INTERVAL_IGNORED   = 32,               //!< Sampling interval paramater ignored. This would be if
                                                             // collection mode is SNAPSHOT or IMMEDIATE
  ASTL_ERROR_INVALID_COLLECTION_MODE              = 33,      //!< Invalid collection mode specified
  ASTL_ERROR_INVALID_COLLECTION_OPTIMIZATION      = 34,      //!< Invalid collection optimization specified
  ASTL_ERROR_COUNTER_NOT_SUPPORTED_ON_TARGET      = 35,      //!< Counter cannot be collected on specified target
  ASTL_ERROR_METRIC_NOT_SUPPORTED_ON_TARGET       = 36,      //!< Metric cannot be collected on specified target
  ASTL_ERROR_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET = 37,      //!< Metric group cannot be collected on specified target
  ASTL_ERROR_COLLECTION_NOT_RUNNING               = 38,  //!< Collection not running. Error when issuing command meant
                                                         // for a running collection
  ASTL_ERROR_COLLECTION_NOT_STOPPED = 39,                //!< Collection not stopped. Error when issuing command meant
                                                         // for a stopped collection
  ASTL_ERROR_COLLECTION_NOT_PAUSED = 40,                 //!< Collection not running. Error when issuing command meant
                                                         // for a paused collection
  ASTL_ERROR_COLLECTION_ALREADY_RUNNING = 41,            //!< Collection already running. Error when issuing command
                                                         // meant to start or resume an already running collection
  ASTL_ERROR_COLLECTION_ALREADY_STOPPED = 42,            //!< Collection already stopped. Error when issuing command
                                                         // meant to stop an already stopped collection
  ASTL_ERROR_COLLECTION_ALREADY_PAUSED = 43,             //!< Collection already paused. Error when issuing command
                                                         // meant to pause an already paused collection
  ASTL_ERROR_NO_DATA_COLLECTED = 44,                     //!< No data collected. Error when attempting to get collected
                                                         // samples but no samples are available.

  // Add new error codes here

  ASTL_ERROR_INTERNAL = 200,  //!< Internal failure

  ASTL_ERROR_UNKOWN = 0xFFFFFFFF,  //!< Unknown error
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
