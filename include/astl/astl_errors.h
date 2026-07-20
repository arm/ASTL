/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file astl_errors.h
 * @brief Public status / error code enumeration and helpers for ASTL.
 *
 * The `astl_status_code` enumeration provides public, user-actionable status
 * reporting for argument validation, version mismatches, buffer sizing issues,
 * unsupported features, collection state transitions, and a generic internal
 * failure. New public codes must remain below 128 (0x80) to preserve existing
 * stringify logic constraints.
 */
#ifndef INCLUDE_ASTL_ERRORS_H_
#define INCLUDE_ASTL_ERRORS_H_

#include "astl/astl_utils.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Public status codes returned by the ASTL C API.
 *
 * Internal implementation failures are normalized to
 * `ASTL_STATUS_INTERNAL_ERROR` before crossing the public C API boundary.
 */
typedef enum _astl_status_code {
  ASTL_STATUS_INTERNAL_ONLY_RANGE_MIN = -4096,  // Reserved internal-only range floor; not returned by the public C API.
  ASTL_STATUS_SUCCESS                 = 0,      //!< Success
  ASTL_STATUS_BAD_ARGUMENT            = 1,      //!< Bad argument passed to function
  ASTL_STATUS_BAD_CONFIGURATION       = 2,      //!< Generic bad configuration error code
  ASTL_STATUS_INVALID_TARGET_HANDLE   = 3,      //!< Invalid target handle used
  ASTL_STATUS_INVALID_COUNTER_HANDLE  = 4,      //!< Invalid counter handle used
  ASTL_STATUS_INVALID_METRIC_HANDLE   = 5,      //!< Invalid metric handle used
  ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE = 6,  //!< Invalid metric group handle used
  ASTL_STATUS_NOT_SUPPORTED               = 8,  //!< Unsupported functionality requested
  ASTL_STATUS_DEPRECATED_API              = 9,  //!< Deprecated API used
  ASTL_STATUS_NO_TARGET_FOUND             = 10,  //!< No targets were detected or configured
  ASTL_STATUS_OLD_STRUCT_VERSION          = 11,  //!< Caller-provided struct size is smaller than expected.
  ASTL_STATUS_NEW_STRUCT_VERSION          = 12,  //!< Caller-provided struct size is larger than expected.
  ASTL_STATUS_NO_COUNTERS_FOUND           = 13,  //!< No counters were detected or configured
  ASTL_STATUS_NO_METRICS_FOUND            = 14,  //!< No metrics were detected or configured
  ASTL_STATUS_NO_METRIC_GROUPS_FOUND      = 15,  //!< No metric groups were detected or configured
  ASTL_STATUS_BUFFER_TOO_SMALL            = 16,  //!< Caller-provided output buffer is too small.
  ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE =
      17,  //!< Metric or MetricManager received a sample that does not match the expected type or value.
  ASTL_STATUS_METRIC_OVERFLOW_DETECTED  = 18,             //!< Overflow detected in metric processing.
  ASTL_STATUS_INVALID_SAMPLING_INTERVAL = 19,             //!< Sampling interval specified is invalid
  ASTL_STATUS_SAMPLING_INTERVAL_IGNORED = 20,             //!< Sampling interval parameter ignored. This would be if
                                                          // collection mode is SNAPSHOT or IMMEDIATE
  ASTL_STATUS_INVALID_COLLECTION_MODE              = 21,  //!< Invalid collection mode specified
  ASTL_STATUS_INVALID_FLAG_VALUE                   = 22,  //!< Invalid flag value specified
  ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET      = 23,  //!< Counter cannot be collected on specified target
  ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET       = 24,  //!< Metric cannot be collected on specified target
  ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET = 25,  //!< Metric group cannot be collected on specified target
  ASTL_STATUS_COLLECTION_NOT_CONFIGURED            = 26,  //!< Collection not configured. Error when starting collection
  ASTL_STATUS_COLLECTION_NOT_RUNNING               = 27,  //!< Collection not running. Error when issuing command meant
                                                          // for a running collection
  ASTL_STATUS_COLLECTION_NOT_STOPPED = 28,                //!< Collection not stopped. Error when issuing command meant
                                                          // for a stopped collection
  ASTL_STATUS_COLLECTION_NOT_PAUSED = 29,                 //!< Collection not running. Error when issuing command meant
                                                          // for a paused collection
  ASTL_STATUS_COLLECTION_ALREADY_RUNNING = 30,            //!< Collection already running. Error when issuing command
                                                          // meant to start or resume an already running collection
  ASTL_STATUS_COLLECTION_ALREADY_STOPPED = 31,            //!< Collection already stopped. Error when issuing command
                                                          // meant to stop an already stopped collection
  ASTL_STATUS_COLLECTION_ALREADY_PAUSED = 32,             //!< Collection already paused. Error when issuing command
                                                          // meant to pause an already paused collection
  ASTL_STATUS_NO_DATA_COLLECTED = 33,                     //!< No data collected. Error when attempting to get collected
                                                          // samples but no samples are available.
  ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED  = 34,            //!< Given buffer  was larger than needed. Not an error.
  ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE = 35,            //!< Unsupported collector type requested.
  ASTL_STATUS_FILE_OPEN_FAILED           = 36,            //!< File exists, but open failed.
  ASTL_STATUS_FILE_ERROR                 = 37,            //!< File system operations failed.
  ASTL_STATUS_OUT_OF_MEMORY              = 38,            //!< Memory allocation failed.
  ASTL_STATUS_INVALID_VALUE_TYPE         = 40,            //!< Invalid astl_value_type_t for operation
  ASTL_STATUS_INVALID_STATE_TRANSITION   = 41,            //!< Generic lifecycle transition not permitted.
  ASTL_STATUS_PAUSE_UNSUPPORTED          = 42,            //!< Collector/hardware cannot pause (treated as no-op error).
  ASTL_STATUS_RESUME_UNSUPPORTED         = 43,  //!< Collector/hardware cannot resume (treated as no-op error).
  // Add new status codes here

  ASTL_STATUS_INTERNAL_ERROR = 127,  //!< Internal failure
  // Do not define public status codes outside 0..127; lower values are reserved for internal-only detail codes.
} astl_status_code;

/**
 * @brief Returns the string version of the Arm SoC Telemetry Library status code
 *
 * @return c-string of astl status code
 */
ASTL_API const char* astlStatusString(astl_status_code status) ASTL_API_NOEXCEPT;

/**
 * @brief Returns the most recent failure detail recorded on the calling thread.
 *
 * The returned pointer remains valid until the next ASTL call on the same
 * thread updates the stored status detail.
 *
 * @return borrowed c-string containing the last status detail, or an empty
 *         string if no failure detail is currently recorded for the thread.
 */
ASTL_API const char* astlGetLastStatusString(void) ASTL_API_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#endif  // INCLUDE_ASTL_ERRORS_H_
