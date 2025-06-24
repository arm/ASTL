#ifndef INCLUDE_ASTL_ERRORS_H_
#define INCLUDE_ASTL_ERRORS_H_

#include "astl/astl_utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Status codes
 * TODO (https://jira.arm.com/browse/ASTL-98) - Create separate list for internal error codes.
 */
typedef enum _astl_status_code {
  ASTL_STATUS_SUCCESS                              = 0,    //!< Success
  ASTL_STATUS_BAD_ARGUMENT                         = 1,    //!< Bad argument passed to function
  ASTL_STATUS_BAD_CONFIGURATION                    = 2,    //!< Generic bad configuration error code
  ASTL_STATUS_INVALID_TARGET_HANDLE                = 3,    //!< Invalid target handle used
  ASTL_STATUS_INVALID_COUNTER_HANDLE               = 4,    //!< Invalid counter handle used
  ASTL_STATUS_INVALID_METRIC_HANDLE                = 5,    //!< Invalid metric handle used
  ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE          = 6,    //!< Invalid metric group handle used
  ASTL_STATUS_NOT_IMPLEMENTED                      = 7,    //!< Functionality not implemented yet
  ASTL_STATUS_NOT_SUPPORTED                        = 8,    //!< Unsupported functionality requested
  ASTL_STATUS_DEPRECATED_API                       = 9,    //!< Deprecated API used
  ASTL_STATUS_NO_TARGETS_FOUND                     = 10,   //!< No targets were detected or configured
  ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION = 11,   //!< The version of the target properties structure used
                                                           // by client is older
  ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION = 12,   //!< The version of the target properties structure used
                                                           // by client is newer
  ASTL_STATUS_NO_COUNTERS_FOUND                     = 13,  //!< No counters were detected or configured
  ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION = 14,  //!< The version of the counter properties structure
                                                           // used by client is older
  ASTL_STATUS_NEW_COUNTER_PROPERTIES_STRUCT_VERSION = 15,  //!< The version of the counter properties structure
                                                           // used by client is newer
  ASTL_STATUS_OLD_COUNTER_SAMPLE_STRUCT_VERSION = 16,      //!< The version of the counter sample structure used by
                                                           // client is older
  ASTL_STATUS_NEW_COUNTER_SAMPLE_STRUCT_VERSION = 17,      //!< The version of the counter sample structure used by
                                                           // client is newer
  ASTL_STATUS_NO_METRICS_FOUND                     = 18,   //!< No metrics were detected or configured
  ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION = 19,   //!< The version of the metric properties structure used
                                                           // by client is older
  ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION = 20,   //!< The version of the metric properties structure used
                                                           // by client is newer
  ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION = 21,       //!< The version of the metric sample structure used by
                                                           // client is older
  ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION = 22,       //!< The version of the metric sample structure used by
                                                           // client is newer
  ASTL_STATUS_NO_METRIC_GROUPS_FOUND = 23,                 //!< No metrics were detected or configured
  ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION =
      24,  //!< The version of the metric group properties structure
           // used by client is older
  ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION =
      25,                                                     //!< The version of the metric group properties structure
                                                              // used by client is newer
  ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION = 26,  //!< The version of the collection parameters structure
                                                              // used by client is older
  ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION = 27,  //!< The version of the collection parameters structure
                                                              // used by client is newer
  ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL = 28,        //!< Buffer of target properties passed in by client is
                                                              // too small to hold all targets
  ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL = 29,       //!< Buffer of counter properties passed in by client is
                                                              // too small to hold all counters
  ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL = 30,        //!< Buffer of metric properties passed in by client is
                                                              // too small to hold all metrics
  ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL = 31,  //!< Buffer of metric group properties passed in by client
                                                              // is too small to hold all metric groups
  ASTL_STATUS_COUNTER_SAMPLES_BUFFER_TOO_SMALL = 32,          //!< Buffer of counter samples passed in by client is too
                                                              // small to hold all counter samples
  ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL = 33,           //!< Buffer of metric samples passed in by client is too
                                                              // small to hold all metric samples
  ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE =
      34,  //!< Metric received a sample that does not match the expected type or value
  ASTL_STATUS_METRIC_OVERFLOW_DETECTED    = 35,           //!< Overflow detected in metric processing.
  ASTL_STATUS_SAMPLING_INTERVAL_TOO_SMALL = 36,           //!< Sampling interval specified is too small
  ASTL_STATUS_SAMPLING_INTERVAL_TOO_LARGE = 37,           //!< Sampling interval specified is too large
  ASTL_STATUS_SAMPLING_INTERVAL_IGNORED   = 38,           //!< Sampling interval paramater ignored. This would be if
                                                          // collection mode is SNAPSHOT or IMMEDIATE
  ASTL_STATUS_INVALID_COLLECTION_MODE              = 39,  //!< Invalid collection mode specified
  ASTL_STATUS_INVALID_COLLECTION_OPTIMIZATION      = 40,  //!< Invalid collection optimization specified
  ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET      = 41,  //!< Counter cannot be collected on specified target
  ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET       = 42,  //!< Metric cannot be collected on specified target
  ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET = 43,  //!< Metric group cannot be collected on specified target
  ASTL_STATUS_COLLECTION_NOT_CONFIGURED            = 44,  //!< Collection not configured. Error when starting collection
  ASTL_STATUS_COLLECTION_NOT_RUNNING               = 45,  //!< Collection not running. Error when issuing command meant
                                                          // for a running collection
  ASTL_STATUS_COLLECTION_NOT_STOPPED = 46,                //!< Collection not stopped. Error when issuing command meant
                                                          // for a stopped collection
  ASTL_STATUS_COLLECTION_NOT_PAUSED = 47,                 //!< Collection not running. Error when issuing command meant
                                                          // for a paused collection
  ASTL_STATUS_COLLECTION_ALREADY_RUNNING = 48,            //!< Collection already running. Error when issuing command
                                                          // meant to start or resume an already running collection
  ASTL_STATUS_COLLECTION_ALREADY_STOPPED = 49,            //!< Collection already stopped. Error when issuing command
                                                          // meant to stop an already stopped collection
  ASTL_STATUS_COLLECTION_ALREADY_PAUSED = 50,             //!< Collection already paused. Error when issuing command
                                                          // meant to pause an already paused collection
  ASTL_STATUS_NO_DATA_COLLECTED = 51,                     //!< No data collected. Error when attempting to get collected
                                                          // samples but no samples are available.
  ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED  = 52,            //!< Given buffer  was larger than needed. Not an error.
  ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE = 53,            //!< Unsupported collector type requested.
  ASTL_STATUS_FILE_OPEN_FAILED           = 54,            //!< File exists, but open failed.
  ASTL_STATUS_FILE_ERROR                 = 55,            //!< File system operations failed.
  ASTL_STATUS_OUT_OF_MEMORY              = 56,            //!< Memory allocation failed.
  ASTL_STATUS_DIVIDE_BY_ZERO             = 57,            //!< Attempted division by zero
  ASTL_STATUS_INVALID_VALUE_TYPE         = 58,            //!< Invalid astl_value_type_t for operation

  // Add new status codes here

  ASTL_STATUS_INTERNAL_ERROR = 200,         //!< Internal failure
  ASTL_STATUS_UNKNOWN_ERROR  = 0xFFFFFFFF,  //!< Unknown error
} astl_status_code;

/**
 * @brief Returns the string version of the Arm SoC Telemetry Library status code
 *
 * @return c-string of astl status code
 */
ASTL_API const char* astlStatusString(astl_status_code status);

#if defined(__cplusplus)
}
#endif

#endif  // INCLUDE_ASTL_ERRORS_H_
