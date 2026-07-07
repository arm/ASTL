/*
 * SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file astl_telemetry.h
 * @brief Core public telemetry collection C API for the Arm SoC Telemetry Library (ASTL).
 *
 * This header exposes functions to enumerate targets,
 * discover counters/metrics/groups, configure collection parameters (sampling,
 * snapshot, immediate), control collection lifecycle (start / pause / resume /
 * stop / immediate read) and retrieve collected counter & metric samples. All
 * API structs include a leading `size` field for versioning; callers MUST set
 * this field to `sizeof(struct_type)` before calling into the API so that
 * forward/backward compatibility can be managed. Buffer-returning APIs follow a
 * two-step pattern: query required counts, allocate & initialize (setting the
 * first element's `size`), then call the getter to populate data. For APIs
 * with caller-provided arrays, getter calls require non-NULL buffers and
 * input capacities > 0; if the discovered required count is 0, skip the
 * corresponding getter call. For APIs that return ASTL_STATUS_BUFFER_TOO_SMALL, the
 * corresponding in/out count is set to the required capacity (when representable
 * in uint32_t).
 */
#ifndef INCLUDE_ASTL_TELEMETRY_H_
#define INCLUDE_ASTL_TELEMETRY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "astl/astl_errors.h"

#if defined(__cplusplus)
extern "C" {
#endif

/***********************************************************************************
 **********************            INITIALIZATION           ************************
 **********************************************************************************/

/**
 * @brief macro to declare a struct of type `type` with name `var` and initialize all fields,
 *        including the size field for API versioning.
 *
 * Compatible with C and C++.
 * Empty variadic arguments are supported only in C++20 or C23 (via `__VA_OPT__`).
 * For older language modes, pass at least one initializer or use `ASTL_INIT_STRUCT_NO_FIELDS`.
 * @example
 * `ASTL_INIT_STRUCT(astl_get_system_info_params_t, params, .flags = 0, .system_info = &platform_info)`
 */
#if (defined(__cplusplus) && __cplusplus >= 202002L) || \
    (!defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#  define ASTL_INIT_STRUCT(type, var, ...) type var = {.size = sizeof(type) __VA_OPT__(, ) __VA_ARGS__}
#else
// Portable fallback for C99/C11/C17 and pre-C++20: requires at least one initializer in __VA_ARGS__.
#  define ASTL_INIT_STRUCT(type, var, ...) type var = {.size = sizeof(type), __VA_ARGS__}
#endif

/**
 * @brief macro to declare a struct of type `type` with name `var` and initialize only `size`.
 */
#define ASTL_INIT_STRUCT_NO_FIELDS(type, var) type var = {.size = sizeof(type)}

/**
 * @brief macro to declare and 0-initialize a `count`-length array of structs of type `type` named `var`
 *        Will initialize the `size` field of the first element in the array for API versioning
 */
#define ASTL_ALLOC_ARRAY(type, var, count)          \
  type* var = (type*)calloc((count), sizeof(type)); \
  if (var && ((count) > 0)) {                       \
    var[0].size = sizeof(type);                     \
  }

/**
 * @brief macro to free an array allocated by ASTL_ALLOC_ARRAY
 */
#define ASTL_FREE_ARRAY(ptr) \
  do {                       \
    if ((ptr) != NULL) {     \
      free(ptr);             \
      (ptr) = NULL;          \
    }                        \
  } while (0)

/***********************************************************************************
 **********************            SYSTEM PROPERTIES         ************************
 **********************************************************************************/

/**
 * @brief System-level platform properties.
 *
 * All string fields point to internal immutable storage owned by ASTL and remain valid
 * until process exit. Any field may be NULL when not available on the running platform.
 */
typedef struct _astl_platform_props_t {
  size_t      size;              //!< Size of this struct for versioning; set size to sizeof(astl_platform_props_t).
  uint32_t    flags;             //!< Source selector input and selected-source output (astl_system_info_flags_t)
  const char* soc_name;          //!< SoC / platform name (for example from sysfs or device-tree)
  const char* vendor_id;         //!< Platform or system vendor identifier
  const char* os_name;           //!< Operating system name
  const char* kernel_name;       //!< Kernel name (for example, "Linux")
  const char* kernel_version;    //!< Kernel version string
  const char* kernel_release;    //!< Kernel release string
  const char* firmware_version;  //!< Firmware or BIOS version if available
  const char* hostname;          //!< Host name
  const char* architecture;      //!< Machine architecture (for example, "aarch64")
  const char* cpu_type;          //!< CPU model/type summary if available
  const char* cpu_features;      //!< CPU feature flags if available
  const char* cache_info;        //!< Cache hierarchy summary if available
  uint32_t    core_count;        //!< Number of configured CPU cores, or 0 when unavailable
  uint32_t    numa_node_count;   //!< Number of NUMA nodes, or 0 when unavailable
  uint32_t    socket_count;      //!< Number of physical CPU packages/sockets, or 0 when unavailable
  uint32_t    cache_line_size_bytes;   //!< Data cache line size in bytes, or 0 when unavailable
  uint64_t    memory_total_bytes;      //!< Total system memory in bytes, or 0 when unavailable
  const char* libc_version;            //!< C library version if available
  const char* boot_info;               //!< Boot environment summary if available
  int64_t     huge_pages_total;        //!< Number of huge pages, or -1 when unavailable
  int64_t     huge_page_size_kb;       //!< Huge page size in KiB, or -1 when unavailable
  const char* transparent_huge_pages;  //!< Transparent huge page mode if available
} astl_platform_props_t;

typedef enum _astl_system_info_flags_t {
  ASTL_SYSTEM_INFO_FLAG_HOST           = (1U << 0),  //!< Use current host system information
  ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION = (1U << 1)   //!< Use loaded-session system information
} astl_system_info_flags_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_system_info_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_system_info_params_t).
  uint32_t flags;  //!< Source selector input/output flags (astl_system_info_flags_t). 0 selects default behavior.
  astl_platform_props_t* system_info;  //!< Output platform properties. Cannot be NULL; set
                                       //!< system_info->size to sizeof(astl_platform_props_t).
} astl_get_system_info_params_t;

/**
 * @brief Get system-level platform information.
 *
 * Source selection:
 *  - By default, returns platform information captured from the current host system.
 *  - After a successful astlLoadCollection(), returns platform information stored in the loaded .astl session.
 *  - After calling any astlConfigure*Collection* API, returns platform information from the current host system
 *    again.
 *
 * @param params Parameters for this call (see astl_get_system_info_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetSystemInfo(const astl_get_system_info_params_t* params) ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************               TARGETS               ************************
 **********************************************************************************/

/** A target can be any level in the system where telemetry can be collected. Could be hardware,
 * firmware, driver, OS or any data source on the system.
 */

typedef const void* astl_target_handle_t;  //!< Abstraction of a target handle

/** A target properties structure describes a target on which telemetry can be collected
 */
typedef struct _astl_target_props_t {
  size_t               size;           //!< Size of this struct for versioning; set size to sizeof(astl_target_props_t).
  astl_target_handle_t handle;         //!< Internal handle to target
  astl_target_handle_t parent_handle;  //!< Internal handle to the parent device where this target
                                       //!< resides. NULL means top-level target.
  const char* name;                    //!< Device name
  const char* description;             //!< Device description
  const char* id;                      //!< Optional null-terminated target identifier string (NULL if not available)
} astl_target_props_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_target_count_params_t {
  size_t    size;          //!< Size of this struct for versioning; set size to sizeof(astl_get_target_count_params_t).
  uint32_t  flags;         //!< Reserved for future flags (must be 0 for now).
  uint32_t* target_count;  //!< Output number of discoverable targets. Cannot be NULL.
} astl_get_target_count_params_t;

/**
 * @brief Get the number of targets on the system on which telemetry collection can be done
 *
 * @param params Parameters for this call (see astl_get_target_count_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetTargetCount(const astl_get_target_count_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_targets_params_t {
  size_t               size;     //!< Size of this struct for versioning; set size to sizeof(astl_get_targets_params_t).
  uint32_t             flags;    //!< Reserved for future flags (must be 0 for now).
  astl_target_props_t* targets;  //!< Caller-allocated target array. Cannot be NULL; set
                                 //!< targets[0].size to sizeof(astl_target_props_t).
  uint32_t* target_count;        //!< In: target-array capacity (> 0). Out: number of elements written.
                                 //!< Cannot be NULL.
} astl_get_targets_params_t;

/**
 * @brief Get properties of all targets on the system on which collection can be done
 *
 * @param params Parameters for this call (see astl_get_targets_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetTargets(const astl_get_targets_params_t* params) ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************               DATA TYPES               *********************
 **********************************************************************************/

/** Generic units we expect to use. The list does not contain units we are unlikely to encounter
 * while collecting telemetry like liters or kilograms for example
 */
typedef enum _astl_units_t {
  ASTL_UNITS_UNKNOWN = -1,      //!< Unknown units
  ASTL_UNITS_NONE    = 0,       //!< No units
  ASTL_UNITS_TICKS   = 1,       //!< Clock ticks for time metric calculation
  ASTL_UNITS_SECONDS = 2,       //!< Time (ticks converted to time). For calculated metric
  ASTL_UNITS_CELSIUS = 3,       //!< Thermal readings in Celsius
  ASTL_UNITS_JOULES  = 4,       //!< Energy readings in joules
  ASTL_UNITS_WATTS   = 5,       //!< Power readings in watts. For calculated metrics but hardware may
                                //!< already be doing the calculation, not ideal but possible
  ASTL_UNITS_VOLTS        = 6,  //!< Voltage readings in volts
  ASTL_UNITS_AMPS         = 7,  //!< Current readings in amps
  ASTL_UNITS_BYTES        = 8,  //!< Bytes transferred
  ASTL_UNITS_MBYTESPERSEC = 9,  //!< Bandwidth in MB/s. For calculated metrics but hardware may
                                //!< already be doing the calculation, not ideal but possible
  ASTL_UNITS_MHZ     = 10,      //!< Frequency readings in MHz
  ASTL_UNITS_RPM     = 11,      //!< Fan speed in revolutions per minute
  ASTL_UNITS_COUNT   = 12,      //!< Count of events or occurrences
  ASTL_UNITS_PERCENT = 13,      //!< Percentage-style readings such as humidity
} astl_units_t;

/** Generic value types we expect to use.
 */
typedef enum _astl_value_type_t {
  ASTL_VALUE_UNKNOWN = -1,  //!< Unknown
  ASTL_VALUE_UINT8   = 0,   //!< Unsigned 8bit integer (char)
  ASTL_VALUE_UINT16  = 1,   //!< Unsigned 16bit integer (short)
  ASTL_VALUE_UINT32  = 2,   //!< Unsigned 32bit integer
  ASTL_VALUE_UINT64  = 3,   //!< Unsigned 64bit integer (long)
  ASTL_VALUE_FLOAT32 = 4,   //!< 32bit float
  ASTL_VALUE_FLOAT64 = 5,   //!< 64bit float (double)
  ASTL_VALUE_BOOL8   = 6,   //!< 8bit boolean
} astl_value_type_t;

/** Value container. Processing of an astl_value_t should be based on astl_value_type_t
 * All readings will use this 64bit union to capture any data 64bit in size or less.
 * If there is a reading that is more than 64bit, more than one counter would be used
 * to capture all reading in up to 64bit chunks
 */
typedef union _astl_value_t {
  uint8_t  ui8;   //!< 8bits unsigned integer for UINT8
  uint16_t ui16;  //!< 16bits unsigned integer for UINT16
  uint32_t ui32;  //!< 32bits unsigned integer for UINT32
  uint64_t ui64;  //!< 64bits unsigned integer for UINT64
  float    fp32;  //!< 32bits float for FLOAT32
  double   fp64;  //!< 64bits float for FLOAT64
  bool     b8;    //!< 8bits boolean for BOOL8
} astl_value_t;

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/

/** A counter contains the most basic raw form of the data. Most of the time, it requires
 * postprocessing to make sense of the data. These raw value containers can be collected directly
 * to minimize post processing overhead or to do different processing with the data elsewhere.
 * It is expected that users of the counter interface would do post processing of the raw data
 * themselves
 */

typedef const void* astl_counter_handle_t;  //!< Abstraction of a counter handle

/** A sample contains the timestamp and value captured for a target/counter or target/metric pair.
 * NOTE: This struct intentionally has no `size` field to minimize the total collected dataset size.
 * Do not use ASTL_INIT_STRUCT for astl_sample_t elements.
 */
typedef struct _astl_sample_t {
  uint64_t     timestamp;  //!< The timestamp when this value was captured
  astl_value_t value;      //!< The value captured
} astl_sample_t;

/** The type of the counter. The type helps decide how to process the counter and how to display the
 * processed data
 */
typedef enum _astl_counter_type_t {
  ASTL_COUNTER_TYPE_UNKNOWN = -1,  //!< Unknown
  ASTL_COUNTER_TYPE_VALUE   = 0,   //!< Point in time value. Example: temperature
  ASTL_COUNTER_TYPE_COUNT   = 1,   //!< Free running count. Example: Bytes transferred, joules,
                                   //!< time residency
  ASTL_COUNTER_TYPE_EVENT = 2,     //!< Point in time event. Example: Wakeup. Events are traced and
                                   //!< cannot be controlled with sampling or immediate reads
} astl_counter_type_t;

/** A counter properties structure describes a counter
 */
typedef struct _astl_counter_props_t {
  size_t                size;         //!< Size of this struct for versioning; set size to sizeof(astl_counter_props_t).
  astl_counter_handle_t handle;       //!< The handle of this counter
  const char*           name;         //!< The name of this counter
  const char*           description;  //!< The description of this counter
  uint32_t              min_sampling_interval;  //!< The minimum sampling interval this counter can be collected
                                                //!< in ms. Example: 10 means counter cannot be collected
                                                //!< faster than every 10ms
  astl_units_t units;                           //!< The raw units of the counter. For example, for memory transfers,
                                                //!< it would be ASTL_UNITS_BYTES. For temperature, it would be
                                                //!< ASTL_UNITS_CELSIUS
  const char* formula;               //!< Transformation required on the counter. Example: & MASK >> 2 DELTA / TIME.
                                     //!< This example would mean: Mask the counter first,
                                     //!<  Then shift the value by 2; do Value2 - Value1 to get a delta and divide the
                                     //!<  result by the elapsed time
  astl_value_type_t value_type;      //!< The type of the value read from the counter. It is used for
                                     //!< interpreting the 64bit reading.
  astl_counter_type_t counter_type;  //!< The counter type. It is used for data processing, output
                                     //!< formatting or visualization
} astl_counter_props_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_counter_count_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_counter_count_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  uint32_t*            counter_count;  //!< Output number of counters on target. Cannot be NULL.
} astl_get_counter_count_params_t;

/**
 * @brief Get the number of telemetry counters that can be collected on the specified target
 *
 * @param params Parameters for this call (see astl_get_counter_count_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetCounterCountOnTarget(const astl_get_counter_count_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_counters_params_t {
  size_t                size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_counters_params_t).
  uint32_t              flags;  //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t  target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_counter_props_t* counters;       //!< Caller-allocated counter array. Cannot be NULL; set
                                        //!< counters[0].size to sizeof(astl_counter_props_t).
  uint32_t* counter_count;              //!< In: counter-array capacity (> 0). Out: number of elements written.
                                        //!< Cannot be NULL.
} astl_get_counters_params_t;

/**
 * @brief Get properties of all counters that can be collected on the specified target
 *
 * @param params Parameters for this call (see astl_get_counters_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetCountersOnTarget(const astl_get_counters_params_t* params) ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************              METRIC                    *********************
 **********************************************************************************/

/** A metric is a usable measurement. It can be a direct 1:1 mapping from a single counter; it can
 * be a post-processed representation of a single counter or it can be a post-processed value from
 * multiple counters. For example, user can request metric "Read Bandwidth". To calculate that, the
 * counter in Bytes has to be read at fixed intervals. The delta between two readings in bytes needs
 * to be divided by 10^6 for MB and then divided by the time interval for MB/s as the final metric
 * value. By requesting to read metric "Read Bandwidth", the library would handle the reading and
 * post processing of the counters.
 */

typedef const void* astl_metric_handle_t;  //!< Abstraction of a metric handle

/** The type of the metric. The type helps decide how to further process, summarize and display metric data
 */
typedef enum _astl_metric_type_t {
  ASTL_METRIC_UNKNOWN          = -1,  //!< Unknown
  ASTL_METRIC_VALUE            = 0,   //!< Single value in a point in time. Example: Temperature
  ASTL_METRIC_FINITE_SET_VALUE = 1,   //!< Single value from a fixed set of possible values in a
                                      //!< point in time. Idle vs Active
  ASTL_METRIC_EVENT = 2,              //!< non-value event in a point in time. Ex: A wakeup event. Events are
                                      //!< traced and cannot be controlled with sampling or immediate reads
  ASTL_METRIC_DELTA = 3,              //!< diff in value when the counter is of type COUNT.
                                      //!< Example: Dropped packets, energy
  ASTL_METRIC_RESIDENCY = 4,          //!< diff in value when the counter is of type time. Ex: Time spent in
                                      //!< a specific power state
  ASTL_METRIC_RATE = 5,               //!< same as DELTA but over time. Example: Bandwidth as MB/s or
                                      //!< Power as energy/s
} astl_metric_type_t;

/** Identifies the specific type of lifecycle event emitted by ASTL into the synthetic
 *  @c astl_lifecycle_events.<target-name> metric.
 *
 *  Consumers retrieve these events via astlGetMetricSamplesOnTarget() using the handle for the
 *  @c astl_lifecycle_events.<target-name> metric; the @c value field of each returned
 *  @c astl_sample_t will be one of the values below, cast to @c uint64_t.
 *
 *  The @c astl_lifecycle_events.<target-name> metric is created lazily, the first time a lifecycle
 *  event actually occurs for the target (i.e. on the first pause, resume, or crop).
 *  Because of this, the metric handle may not appear in a metric list retrieved via
 *  astlGetMetricsOnTarget() before the first lifecycle event; callers that cached the metric list
 *  earlier must rediscover the metrics for the target (call astlGetMetricCountOnTarget() then
 *  astlGetMetricsOnTarget() again) after the first pause/resume/crop to obtain the handle.
 */
typedef enum _astl_lifecycle_event_type_t {
  ASTL_LIFECYCLE_EVENT_PAUSE      = 0,  //!< Collection paused on this target
  ASTL_LIFECYCLE_EVENT_RESUME     = 1,  //!< Collection resumed on this target
  ASTL_LIFECYCLE_EVENT_CROP_BEGIN = 2,  //!< Start boundary of a sample crop window applied to this target
  ASTL_LIFECYCLE_EVENT_CROP_END   = 3,  //!< End boundary of a sample crop window applied to this target
} astl_lifecycle_event_type_t;

/** High-level identifier of a metric. Derived from configuration JSON "identifier" string. */
typedef enum _astl_metric_identifier_t {
  ASTL_METRIC_IDENTIFIER_UNKNOWN       = -1,  //!< Unknown or unmapped identifier
  ASTL_METRIC_IDENTIFIER_COUNT         = 0,   //!< Count-based metrics (monotonic counters, event counts)
  ASTL_METRIC_IDENTIFIER_TEMPERATURE   = 1,   //!< Thermal metrics (temperature sensors)
  ASTL_METRIC_IDENTIFIER_THERMAL_LIMIT = 2,  //!< Thermal threshold or limit metrics (min/max/critical/emergency limits)
  ASTL_METRIC_IDENTIFIER_THERMAL_THROTTLE = 3,   //!< Thermal throttling state or event metrics
  ASTL_METRIC_IDENTIFIER_ENERGY           = 4,   //!< Energy metrics (joules)
  ASTL_METRIC_IDENTIFIER_POWER            = 5,   //!< Power metrics (instantaneous or accumulated energy rate)
  ASTL_METRIC_IDENTIFIER_POWER_LIMIT      = 6,   //!< Power threshold or limit metrics
  ASTL_METRIC_IDENTIFIER_POWER_THROTTLE   = 7,   //!< Power throttling state or event metrics
  ASTL_METRIC_IDENTIFIER_FREQUENCY        = 8,   //!< Frequency metrics (clock rates)
  ASTL_METRIC_IDENTIFIER_VOLTAGE          = 9,   //!< Voltage metrics
  ASTL_METRIC_IDENTIFIER_CURRENT          = 10,  //!< Current metrics (amperage)
  ASTL_METRIC_IDENTIFIER_BANDWIDTH        = 11,  //!< Bandwidth metrics (data transfer rates)
  ASTL_METRIC_IDENTIFIER_FAN_SPEED        = 12,  //!< Fan speed metrics (RPM)
  ASTL_METRIC_IDENTIFIER_HUMIDITY         = 13,  //!< Humidity metrics (percent)
  ASTL_METRIC_IDENTIFIER_STATUS           = 14,  //!< Boolean or status-style metrics (alarms, faults, enables)
} astl_metric_identifier_t;

/** A metric properties structure describes a metric
 */
typedef struct _astl_metric_props_t {
  size_t               size;         //!< Size of this struct for versioning; set size to sizeof(astl_metric_props_t).
  astl_metric_handle_t handle;       //!< Handle for this metric
  const char*          name;         //!< The name of this metric
  const char*          description;  //!< The description of this metric
  uint32_t             min_sampling_interval;  //!< The minimum sampling interval this metric can be collected
                                               //!< in ms. It is the largest minimum sampling interval
                                               //!< value from all counters used in this metric
  astl_units_t units;                          //!< The units of the metric. For example, for bandwidth, it would be
                                               //!< ASTL_UNITS_MBYTESPERSEC
  astl_value_type_t value_type;                //!< The type of the processed value from the metric. It is used
                                               //!< for interpreting the 64bit metric value
  astl_metric_type_t metric_type;              //!< The metric type. It is used for output formatting and
                                               //!< visualization
  astl_metric_identifier_t identifier;         //!< High-level identifier such as POWER, TEMPERATURE etc.
} astl_metric_props_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_count_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_metric_count_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  uint32_t*            metric_count;   //!< Output number of metrics on target. Cannot be NULL.
} astl_get_metric_count_params_t;

/**
 * @brief Get the number of telemetry metrics that can be collected on the specified target
 *
 * @param params Parameters for this call (see astl_get_metric_count_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricCountOnTarget(const astl_get_metric_count_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metrics_params_t {
  size_t               size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_metrics_params_t).
  uint32_t             flags;  //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_props_t* metrics;        //!< Caller-allocated metric array. Cannot be NULL; set
                                       //!< metrics[0].size to sizeof(astl_metric_props_t).
  uint32_t* metric_count;              //!< In: metric-array capacity (> 0). Out: number of elements written.
                                       //!< Cannot be NULL.
} astl_get_metrics_params_t;

/**
 * @brief Get properties of all metrics that can be collected on the specified target
 *
 * @param params Parameters for this call (see astl_get_metrics_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricsOnTarget(const astl_get_metrics_params_t* params) ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************      METRIC VALUE/STATE DISCOVERY      *********************
 **********************************************************************************/

/** A state name structure describes a state or value for metrics with discrete states.
 * Used by both finite set metrics (ASTL_METRIC_FINITE_SET_VALUE) and residency metrics
 * (ASTL_METRIC_RESIDENCY).
 *
 * For finite set metrics: value contains the possible value, name provides a human-readable label.
 * For residency metrics: name contains the state name (e.g., "C6", "C1", "Active"), value is unused.
 *
 * The order of states returned defines the sequence in which processed samples are reported.
 */
typedef struct _astl_state_props_t {
  size_t       size;         //!< Size of this struct for versioning; set size to sizeof(astl_state_props_t).
  const char*  name;         //!< State/label name (always set)
  const char*  description;  //!< Description of the state (always set)
  astl_value_t value;        //!< The value (only used for finite set metrics)
} astl_state_props_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_state_count_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_get_metric_state_count_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  uint32_t*            state_count;    //!< Output number of states/values for the metric. Cannot be NULL.
} astl_get_metric_state_count_on_target_params_t;

/**
 * @brief Get the number of state names for a metric.
 *
 * This function is applicable to metrics of type ASTL_METRIC_FINITE_SET_VALUE
 * or ASTL_METRIC_RESIDENCY. It returns the count of possible states/values.
 *
 * For finite set metrics: Returns the count of possible enumerated values.
 * For residency metrics: Returns the count of tracked states (including any inferred state).
 *
 * @param params Parameters for this call (see astl_get_metric_state_count_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricStateCountOnTarget(const astl_get_metric_state_count_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_states_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_get_metric_states_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  astl_state_props_t*  states;         //!< Caller-allocated state array. Cannot be NULL; set
                                       //!< states[0].size to sizeof(astl_state_props_t).
  uint32_t* state_count;               //!< In: state-array capacity (> 0). Out: number of elements written.
                                       //!< Cannot be NULL.
} astl_get_metric_states_on_target_params_t;

/**
 * @brief Get the state names for a metric.
 *
 * This function retrieves the states for finite set or residency metrics.
 *
 * For finite set metrics (ASTL_METRIC_FINITE_SET_VALUE):
 *   - Returns all possible enumerated values with their labels
 *   - Each element has both value and name populated
 *   - value contains the possible value, name contains the human-readable label
 *
 * For residency metrics (ASTL_METRIC_RESIDENCY):
 *   - Returns state names in the order samples are reported
 *   - Each element has name populated with the state name (e.g., "C6", "C1")
 *   - value field is unused for residency metrics
 *   - If an inferred state exists, it appears as the last element
 *
 * @param params Parameters for this call (see astl_get_metric_states_on_target_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricStatesOnTarget(const astl_get_metric_states_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************              METRIC GROUPS             *********************
 **********************************************************************************/

/** A metric group is a way to request collection of multiple metrics at the same time. The grouping
 * is statically predefined for specific platforms. The grouping is meant to be a shorthand for
 * collecting multiple metrics at the same time for a specific target. For example, group
 * "Bandwidth" can include all bandwidth telemetry sources on the system: memory controller
 * bandwidth, interconnect transfers, data packets, pcie transfers etc. "Temperature" group can
 * include all telemetry from all thermal sensors on the system. Note: Collected data will be
 * available for each metric in the metric group. When using metric groups, one needs to discover
 * the metrics within the metric groups to interpret the data.
 */

typedef const void* astl_metric_group_handle_t;  //!< Abstraction of a metric group handle

/** A metric group properties structure describes a metric group.
 */
typedef struct _astl_metric_group_props_t {
  size_t size;  //!< Size of this struct for versioning; set size to sizeof(astl_metric_group_props_t).
  astl_metric_group_handle_t handle;       //!< The handle of this metric group
  const char*                name;         //!< The name of this metric group
  const char*                description;  //!< The description of this metric group
} astl_metric_group_props_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_group_count_params_t {
  size_t    size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_metric_group_count_params_t).
  uint32_t  flags;  //!< Reserved for future flags (must be 0 for now).
  uint32_t* metric_group_count;  //!< Output number of metric groups across all targets. Cannot be NULL.
} astl_get_metric_group_count_params_t;

/**
 * @brief Get the number of telemetry metric groups defined across all targets
 *
 * @param params Parameters for this call (see astl_get_metric_group_count_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroupCount(const astl_get_metric_group_count_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_groups_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_metric_groups_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  astl_metric_group_props_t* metric_groups;  //!< Caller-allocated metric-group array. Cannot be NULL;
                                             //!< set metric_groups[0].size to sizeof(astl_metric_group_props_t).
  uint32_t* metric_group_count;              //!< In: group-array capacity (> 0). Out: number of elements written.
                                             //!< Cannot be NULL.
} astl_get_metric_groups_params_t;

/**
 * @brief Get properties of all metric groups defined across all targets
 *
 * @param params Parameters for this call (see astl_get_metric_groups_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroups(const astl_get_metric_groups_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_group_count_on_target_params_t {
  size_t size;                              //!< Size of this struct for versioning; set size to
                                            //!< sizeof(astl_get_metric_group_count_on_target_params_t).
  uint32_t             flags;               //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;       //!< Target handle of interest from astl_target_props_t.
  uint32_t*            metric_group_count;  //!< Output number of metric groups on target. Cannot be NULL.
} astl_get_metric_group_count_on_target_params_t;

/**
 * @brief Get the number of telemetry metric groups defined for the specified target
 *
 * @param params Parameters for this call (see astl_get_metric_group_count_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroupCountOnTarget(const astl_get_metric_group_count_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_groups_on_target_params_t {
  size_t size;  //!< Size of this struct for versioning; set size to sizeof(astl_get_metric_groups_on_target_params_t).
  uint32_t                   flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t       target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_group_props_t* metric_groups;  //!< Caller-allocated metric-group array. Cannot be NULL;
                                             //!< set metric_groups[0].size to sizeof(astl_metric_group_props_t).
  uint32_t* metric_group_count;              //!< In: group-array capacity (> 0). Out: number of elements written.
                                             //!< Cannot be NULL.
} astl_get_metric_groups_on_target_params_t;

/**
 * @brief Get properties of all metric groups defined for the specified target
 *
 * @param params Parameters for this call (see astl_get_metric_groups_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroupsOnTarget(const astl_get_metric_groups_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_group_metric_count_params_t {
  size_t size;                                     //!< Size of this struct for versioning; set size to
                                                   //!< sizeof(astl_get_metric_group_metric_count_params_t).
  uint32_t                   flags;                //!< Reserved for future flags (must be 0 for now).
  astl_metric_group_handle_t metric_group_handle;  //!< Metric-group handle. Cannot be NULL.
  uint32_t*                  metric_count;         //!< Output number of metrics in the group. Cannot be NULL.
} astl_get_metric_group_metric_count_params_t;

/**
 * @brief Get the number of metrics that can be collected regardless of target that are part of the
 * specified metric group
 *
 * @param params Parameters for this call (see astl_get_metric_group_metric_count_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroupMetricCount(const astl_get_metric_group_metric_count_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_group_metrics_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_get_metric_group_metrics_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  astl_metric_group_handle_t metric_group_handle;  //!< Metric-group handle. Cannot be NULL.
  astl_metric_props_t*       metrics;  //!< Caller-allocated metric array. Cannot be NULL; set metrics[0].size
                                       //!< to sizeof(astl_metric_props_t).
  uint32_t* metric_count;              //!< In: metric-array capacity (> 0). Out: number of metrics in the group.
                                       //!< Cannot be NULL.
} astl_get_metric_group_metrics_params_t;

/**
 * @brief Get properties of all metrics that can be collected regardless of target that are part
 * of the specified metric group
 *
 * @param params Parameters for this call (see astl_get_metric_group_metrics_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroupMetrics(const astl_get_metric_group_metrics_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_group_metric_count_on_target_params_t {
  size_t size;                                     //!< Size of this struct for versioning; set size to
                                                   //!< sizeof(astl_get_metric_group_metric_count_on_target_params_t).
  uint32_t                   flags;                //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t       target_handle;        //!< Target handle of interest from astl_target_props_t.
  astl_metric_group_handle_t metric_group_handle;  //!< Metric-group handle. Cannot be NULL.
  uint32_t*                  metric_count;         //!< Output number of metrics in the group on target. Cannot be NULL.
} astl_get_metric_group_metric_count_on_target_params_t;

/**
 * @brief Get the number of metrics that can be collected on the specified target that are part of
 * the specified metric group
 *
 * @param params Parameters for this call (see astl_get_metric_group_metric_count_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricGroupMetricCountOnTarget(
    const astl_get_metric_group_metric_count_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_group_metrics_on_target_params_t {
  size_t size;                                     //!< Size of this struct for versioning; set size to
                                                   //!< sizeof(astl_get_metric_group_metrics_on_target_params_t).
  uint32_t                   flags;                //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t       target_handle;        //!< Target handle of interest from astl_target_props_t.
  astl_metric_group_handle_t metric_group_handle;  //!< Metric-group handle. Cannot be NULL.
  astl_metric_props_t*       metrics;  //!< Caller-allocated metric array. Cannot be NULL; set metrics[0].size
                                       //!< to sizeof(astl_metric_props_t).
  uint32_t* metric_count;  //!< In: metric-array capacity (> 0). Out: number of metrics in the group on target.
                           //!< Cannot be NULL.
} astl_get_metric_group_metrics_on_target_params_t;

/**
 * @brief Get properties of all metrics that can be collected on the specified target that are part
 * of the specified metric group
 *
 * @param params Parameters for this call (see astl_get_metric_group_metrics_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code
astlGetMetricGroupMetricsOnTarget(const astl_get_metric_group_metrics_on_target_params_t* params) ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************              COLLECTION                *********************
 **********************************************************************************/

/* Collection configuration and control section. Users are expected to use an
 * `astl_collection_params_t` instance to set the parameters and pass the structure to the
 * configure API. Any subsequent configure collection call using any of the configure API starts a
 * clean collection session when collection is not running. If a previous collection has reached
 * STOPPED, retrieve or save any samples you still need before configuring again. The next
 * configure call clears collection-scoped data such as cached samples, processed samples, clock
 * correlations, and operation mappings. Target discovery, metric definitions, and metric handles
 * remain available. Collection configurations are per target: Only one of the three ways to
 * collect is possible for a given target at any given time; i.e. Either counter, metric or metric
 * group collection per target.
 */

/** The collection mode. The mode configures how and when telemetry data is read
 */
typedef enum _astl_collection_mode_t {
  ASTL_COLLECTION_MODE_SAMPLING  = 0,  //!< Fixed time interval collection mode. Example: every 100ms
  ASTL_COLLECTION_MODE_IMMEDIATE = 1,  //!< Data is captured only when the user makes a read API call
                                       //!< for immediate capturing of the data.
  ASTL_COLLECTION_MODE_SNAPSHOT = 2,   //!< Data points are captured when start collection and stop
                                       //!< collection API are called
} astl_collection_mode_t;

/** Collection-parameter flag masks.
 * Select one optimization preference. Use ASTL_COLLECTION_PARAMETERS_FLAG_NONE for no optimization.
 */
typedef enum _astl_collection_parameters_flags_t {
  ASTL_COLLECTION_PARAMETERS_FLAG_NONE                  = 0U,         //!< No optimization preference
  ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD     = (1U << 0),  //!< Minimize performance overhead
  ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY       = (1U << 1),  //!< Minimize memory usage
  ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_INTERFERENCE = (1U << 2),  //!< Minimize disruption of normal behavior
                                                                      //!< of the system
  ASTL_NO_CACHING = (1U << 3),  //!< Keep samples in memory for retrieval without retaining raw samples for
                                //!< ASTL serialization.
} astl_collection_parameters_flags_t;

/** collection parameter structure describes parameters for the collection
 */
typedef struct _astl_collection_params_t {
  size_t   size;               //!< Size of this struct for versioning; set size to sizeof(astl_collection_params_t).
  uint32_t flags;              //!< Collection behavior flags (ASTL_COLLECTION_PARAMETERS_FLAG_* and ASTL_NO_CACHING).
  uint32_t sampling_interval;  //!< optional, used to set the sampling interval in ms if the
                               //!< collection mode is set to ASTL_COLLECTION_MODE_SAMPLING.
  astl_collection_mode_t collection_mode;  //!< SAMPLING, SNAPSHOT, IMMEDIATE.
                                           //!< Note: Traced events are not configurable;
                                           //!< they are captured when they happen
} astl_collection_params_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_configure_counter_collection_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_configure_counter_collection_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  const astl_collection_params_t*
      collection_params;                         //!< Collection parameters. Cannot be NULL;
                                                 //!< collection_params->size must be sizeof(astl_collection_params_t).
  const astl_counter_handle_t* counter_handles;  //!< Counter handles to collect. Cannot be NULL.
  uint32_t                     counter_count;    //!< Number of handles in counter_handles.
} astl_configure_counter_collection_on_target_params_t;

/**
 * @brief Configure a counter collection for a specific target
 *
 * @param params Parameters for this call (see astl_configure_counter_collection_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlConfigureCounterCollectionOnTarget(
    const astl_configure_counter_collection_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_configure_counter_collection_params_t {
  size_t size;     //!< Size of this struct for versioning; set size to
                   //!< sizeof(astl_configure_counter_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  const astl_collection_params_t*
      collection_params;  //!< Collection parameters. Cannot be NULL;
                          //!< collection_params->size must be sizeof(astl_collection_params_t).
  const astl_counter_handle_t*
           counter_handles;  //!< Counter handles to collect across all applicable targets. Cannot be NULL.
  uint32_t counter_count;    //!< Number of handles in counter_handles.
} astl_configure_counter_collection_params_t;

/**
 * @brief Configure a counter collection for all targets on which the specified counters can be
 * collected
 *
 * @param params Parameters for this call (see astl_configure_counter_collection_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlConfigureCounterCollection(const astl_configure_counter_collection_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_configure_metric_collection_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_configure_metric_collection_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  const astl_collection_params_t*
      collection_params;                       //!< Collection parameters. Cannot be NULL;
                                               //!< collection_params->size must be sizeof(astl_collection_params_t).
  const astl_metric_handle_t* metric_handles;  //!< Metric handles to collect. Cannot be NULL.
  uint32_t                    metric_count;    //!< Number of handles in metric_handles.
} astl_configure_metric_collection_on_target_params_t;

/**
 * @brief Configure a metric collection for a specific target
 *
 * @param params Parameters for this call (see astl_configure_metric_collection_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlConfigureMetricCollectionOnTarget(
    const astl_configure_metric_collection_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_configure_metric_collection_params_t {
  size_t size;     //!< Size of this struct for versioning; set size to
                   //!< sizeof(astl_configure_metric_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  const astl_collection_params_t*
      collection_params;  //!< Collection parameters. Cannot be NULL;
                          //!< collection_params->size must be sizeof(astl_collection_params_t).
  const astl_metric_handle_t*
           metric_handles;  //!< Metric handles to collect across all applicable targets. Cannot be NULL.
  uint32_t metric_count;    //!< Number of handles in metric_handles.
} astl_configure_metric_collection_params_t;

/**
 * @brief Configure a metric collection for all targets on which the specified metrics can be
 * collected
 *
 * @param params Parameters for this call (see astl_configure_metric_collection_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlConfigureMetricCollection(const astl_configure_metric_collection_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_configure_metric_group_collection_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_configure_metric_group_collection_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  const astl_collection_params_t*
      collection_params;  //!< Collection parameters. Cannot be NULL;
                          //!< collection_params->size must be sizeof(astl_collection_params_t).
  const astl_metric_group_handle_t* metric_group_handles;  //!< Metric-group handles to collect. Cannot be NULL.
  uint32_t                          metric_group_count;    //!< Number of handles in metric_group_handles.
} astl_configure_metric_group_collection_on_target_params_t;

/**
 * @brief Configure a metric group collection for a specific target
 *
 * If multiple selected groups contain the same metric, ASTL deduplicates that metric and collects it only once.
 *
 * @param params Parameters for this call (see astl_configure_metric_group_collection_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlConfigureMetricGroupCollectionOnTarget(
    const astl_configure_metric_group_collection_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_configure_metric_group_collection_params_t {
  size_t size;     //!< Size of this struct for versioning; set size to
                   //!< sizeof(astl_configure_metric_group_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  const astl_collection_params_t*
      collection_params;  //!< Collection parameters. Cannot be NULL;
                          //!< collection_params->size must be sizeof(astl_collection_params_t).
  const astl_metric_group_handle_t* metric_group_handles;  //!< Metric-group handles to collect across all applicable
                                                           //!< targets. Cannot be NULL.
  uint32_t metric_group_count;                             //!< Number of handles in metric_group_handles.
} astl_configure_metric_group_collection_params_t;

/**
 * @brief Configure a metric group collection for all targets on which the specified metric groups can be
 * collected
 *
 * Each target is configured only with the subset of the requested groups that it supports. If multiple selected
 * groups on a target contain the same metric, ASTL deduplicates that metric and collects it only once for that
 * target.
 *
 * @param params Parameters for this call (see astl_configure_metric_group_collection_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code
astlConfigureMetricGroupCollection(const astl_configure_metric_group_collection_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_read_immediate_on_target_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_read_immediate_on_target_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
} astl_read_immediate_on_target_params_t;

/**
 * @brief Do an immediate sample capture of configured counters or metrics on a specific target
 *
 * This can be called while collection is configured, started, or paused.
 *
 * @param params Parameters for this call (see astl_read_immediate_on_target_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlReadImmediateOnTarget(const astl_read_immediate_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_read_immediate_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_read_immediate_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
} astl_read_immediate_params_t;

/**
 * @brief Do an immediate sample capture of configured counters or metrics on all configured targets
 *
 * This reads targets with configured, started, or paused collection state.
 *
 * @param params Parameters for this call (see astl_read_immediate_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlReadImmediate(const astl_read_immediate_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_start_collection_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_start_collection_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle to start.
} astl_start_collection_on_target_params_t;

/**
 * @brief Start telemetry collection on a specific target
 *
 * @param params Parameters for this call (see astl_start_collection_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlStartCollectionOnTarget(const astl_start_collection_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_start_collection_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_start_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
} astl_start_collection_params_t;

/**
 * @brief Start telemetry collection on all targets
 *
 * @param params Parameters for this call (see astl_start_collection_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlStartCollection(const astl_start_collection_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_start_collection_on_target_paused_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_start_collection_on_target_paused_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle to start and leave paused.
} astl_start_collection_on_target_paused_params_t;

/**
 * @brief Start telemetry collection on a specific target and immediately leave it paused
 *
 * Collection setup/start is performed, but sampling is paused before the call returns.
 *
 * @param params Parameters for this call (see astl_start_collection_on_target_paused_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code
astlStartCollectionOnTargetPaused(const astl_start_collection_on_target_paused_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_start_collection_paused_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_start_collection_paused_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
} astl_start_collection_paused_params_t;

/**
 * @brief Start telemetry collection on all configured targets and immediately leave it paused
 *
 * Collection setup/start is performed, but sampling is paused before the call returns.
 *
 * @param params Parameters for this call (see astl_start_collection_paused_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlStartCollectionPaused(const astl_start_collection_paused_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_pause_collection_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_pause_collection_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle to pause.
} astl_pause_collection_on_target_params_t;

/**
 * @brief Pause telemetry collection on a specific target
 *
 * @param params Parameters for this call (see astl_pause_collection_on_target_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlPauseCollectionOnTarget(const astl_pause_collection_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_pause_collection_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_pause_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
} astl_pause_collection_params_t;

/**
 * @brief Pause telemetry collection on all targets
 *
 * @param params Parameters for this call (see astl_pause_collection_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlPauseCollection(const astl_pause_collection_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_resume_collection_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_resume_collection_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle to resume.
} astl_resume_collection_on_target_params_t;

/**
 * @brief Resume telemetry collection on a specific target
 *
 * @param params Parameters for this call (see astl_resume_collection_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlResumeCollectionOnTarget(const astl_resume_collection_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_resume_collection_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_resume_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
} astl_resume_collection_params_t;

/**
 * @brief Resume telemetry collection on all targets
 *
 * @param params Parameters for this call (see astl_resume_collection_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlResumeCollection(const astl_resume_collection_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_stop_collection_on_target_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_stop_collection_on_target_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle to stop.
} astl_stop_collection_on_target_params_t;

/**
 * @brief Stop telemetry collection on a specific target
 *
 * @param params Parameters for this call (see astl_stop_collection_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlStopCollectionOnTarget(const astl_stop_collection_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_stop_collection_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_stop_collection_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
} astl_stop_collection_params_t;

/**
 * @brief Stop telemetry collection on all targets
 *
 * @param params Parameters for this call (see astl_stop_collection_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlStopCollection(const astl_stop_collection_params_t* params) ASTL_API_NOEXCEPT;

/*** Save/Load collection session to/from .astl file ***/
/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_save_params_t {
  size_t      size;              //!< Size of this struct for versioning; set size to sizeof(astl_save_params_t).
  uint32_t    flags;             //!< Reserved for future flags (must be 0 for now)
  const char* output_file_path;  //!< Path (absolute or relative) of the final .astl file to generate.
} astl_save_params_t;

/**
 * @brief Save collected samples to a .astl file.
 *        Should be called after collection is stopped to persist collected data to a .astl file.
 *
 * @param params Parameters for this call (see astl_save_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlSaveCollection(const astl_save_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_load_params_t {
  size_t      size;              //!< Size of this struct for versioning; set size to sizeof(astl_load_params_t).
  uint32_t    flags;             //!< Reserved for future flags (must be 0 for now)
  const char* input_file_path;   //!< Path to the .astl file to load. Must be non-null/non-empty.
  size_t      chunk_size_bytes;  //!< Chunk size in bytes for segmented reads; 0 uses library default.
} astl_load_params_t;

/**
 * @brief Load a previously saved .astl file for post-processing only.
 *        After loading, collection control APIs (Start/Pause/Resume/Stop) are disabled
 *        until the user calls a Configure* API, which resets ASTL to allow new collection.
 *
 * @param params Parameters for this call (see astl_load_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlLoadCollection(const astl_load_params_t* params) ASTL_API_NOEXCEPT;

/*** COLLECTED COUNTER SAMPLES ***/
/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_counter_sample_count_on_target_params_t {
  size_t size;                           //!< Size of this struct for versioning; set size to
                                         //!< sizeof(astl_get_counter_sample_count_on_target_params_t).
  uint32_t              flags;           //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t  target_handle;   //!< Target handle of interest from astl_target_props_t.
  astl_counter_handle_t counter_handle;  //!< Counter handle of interest from astl_counter_props_t.
  uint32_t*             sample_count;    //!< Output number of collected samples for (target_handle, counter_handle).
                                         //!< When filtering is active, reflects the filtered count.
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_counter_sample_count_on_target_params_t;

/**
 * @brief Get the number of samples collected for specific counter on a specific target
 *
 * @param params Parameters for this call (see astl_get_counter_sample_count_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code
astlGetCounterSampleCountOnTarget(const astl_get_counter_sample_count_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_counter_samples_on_target_params_t {
  size_t size;                           //!< Size of this struct for versioning; set size to
                                         //!< sizeof(astl_get_counter_samples_on_target_params_t).
  uint32_t              flags;           //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t  target_handle;   //!< Target handle of interest from astl_target_props_t.
  astl_counter_handle_t counter_handle;  //!< Counter handle of interest from astl_counter_props_t.
  astl_sample_t*        samples;         //!< Caller-allocated sample array. Cannot be NULL.
  uint32_t*             sample_count;    //!< In: sample-array capacity (> 0). Out: number of samples written.
                                         //!< Cannot be NULL.
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_counter_samples_on_target_params_t;

/**
 * @brief Get the samples collected for specific counter on a specific target
 *
 * @param params Parameters for this call (see astl_get_counter_samples_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetCounterSamplesOnTarget(const astl_get_counter_samples_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/*** COLLECTED METRIC SAMPLES ***/
/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_sample_count_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_get_metric_sample_count_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  uint32_t*            sample_count;   //!< Output number of collected samples for (target_handle, metric_handle).
                                       //!< When filtering is active, reflects the filtered count.
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_metric_sample_count_on_target_params_t;

/**
 * @brief Get the number of samples collected for specific metric on a specific target
 *
 * @param params Parameters for this call (see astl_get_metric_sample_count_on_target_params_t).
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code
astlGetMetricSampleCountOnTarget(const astl_get_metric_sample_count_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_samples_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_get_metric_samples_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  astl_sample_t*       samples;        //!< Caller-allocated sample array. Cannot be NULL.
  uint32_t*            sample_count;   //!< In: sample-array capacity (> 0). Out: number of samples written.
                                       //!< Cannot be NULL.
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_metric_samples_on_target_params_t;

/**
 * @brief Get the samples collected for specific metric on a specific target
 *
 * @param params Parameters for this call (see astl_get_metric_samples_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricSamplesOnTarget(const astl_get_metric_samples_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************          METRIC SUMMARY API         ************************
 **********************************************************************************/

/** Flags controlling how metric averages are computed in astl_metric_statistics_t. */
typedef enum _astl_metric_statistics_flags_t {
  ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG       = (1U << 0),  //!< Compute arithmetic mean across samples.
  ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG = (1U << 1)   //!< Compute time-weighted average using sample intervals.
} astl_metric_statistics_flags_t;

/**
 * @brief Structure to hold min/max/average summary statistics for a metric.
 *
 * This structure contains the computed minimum, maximum, and average values
 * for a collected metric on a specific target, along with the number of samples
 * used in the computation.
 */
typedef struct _astl_metric_statistics_t {
  size_t       size;   //!< Size of this struct for versioning; set size to sizeof(astl_metric_statistics_t).
  uint32_t     flags;  //!< Average-mode selector input and selected-mode output (astl_metric_statistics_flags_t).
  astl_value_t min;    //!< Minimum value. Union member matches the metric's value type.
  astl_value_t max;    //!< Maximum value. Union member matches the metric's value type.
  astl_value_t avg;    //!< Average value. Always stored as fp64 regardless of the metric's
                       //!< value type (including integer metrics). Always read avg.fp64.
  uint64_t count;      //!< Number of samples processed.
} astl_metric_statistics_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_statistics_on_target_params_t {
  size_t size;                              //!< Size of this struct for versioning; set size to
                                            //!< sizeof(astl_get_metric_statistics_on_target_params_t).
  uint32_t                  flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t      target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t      metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  astl_metric_statistics_t* summary;        //!< Output summary structure. Cannot be NULL; set
                                            //!< summary->size to sizeof(astl_metric_statistics_t).
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_metric_statistics_on_target_params_t;

/**
 * @brief Get the min/max/average summary for a specific metric on a specific target.
 *
 * This function computes statistical summary (minimum, maximum, and average) for
 * all collected samples of a given metric on a target.
 *
 * The count field indicates the number of samples processed. If count > 0,
 * the min, max, and avg fields contain valid values. If count == 0,
 * no samples were available and the min/max/avg fields should not be used.
 *
 *
 * @param params Parameters for this call (see astl_get_metric_statistics_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricStatisticsOnTarget(const astl_get_metric_statistics_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/**
 * @brief A single bin in a discrete histogram.
 *
 * Each bin represents one unique value observed in the collected samples and the
 * number of times that exact value appeared.
 */
typedef struct _astl_discrete_histogram_bin_t {
  size_t       size;   //!< Size of this struct for versioning; set size to sizeof(astl_discrete_histogram_bin_t).
  astl_value_t value;  //!< The exact value for this bin. Union member matches the metric's value type.
  uint64_t     count;  //!< Number of samples whose value exactly equals value.
} astl_discrete_histogram_bin_t;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_discrete_histogram_bin_count_on_target_params_t {
  size_t size;                         //!< Size of this struct for versioning; set size to
                                       //!< sizeof(astl_get_metric_discrete_histogram_bin_count_on_target_params_t).
  uint32_t             flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  uint32_t*            bin_count;      //!< Output number of unique-value bins. Cannot be NULL.
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_metric_discrete_histogram_bin_count_on_target_params_t;

/**
 * @brief Query the number of discrete histogram bins for a specific metric on a specific target.
 *
 * This is step 1 of the two-step discrete histogram API. The returned @p bin_count
 * must be used to allocate an array of astl_discrete_histogram_bin_t large enough
 * to receive all bins in the subsequent call to astlGetMetricDiscreteHistogramOnTarget().
 *
 * A bin corresponds to one unique value observed across all collected samples for the
 * given (target, metric) pair. If no samples were collected, @p bin_count is set to 0.
 *
 * @param params Parameters for this call (see astl_get_metric_discrete_histogram_bin_count_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricDiscreteHistogramBinCountOnTarget(
    const astl_get_metric_discrete_histogram_bin_count_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for this API call.
 */
typedef struct astl_get_metric_discrete_histogram_on_target_params_t {
  size_t size;                                   //!< Size of this struct for versioning; set size to
                                                 //!< sizeof(astl_get_metric_discrete_histogram_on_target_params_t).
  uint32_t                       flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t           target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t           metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  astl_discrete_histogram_bin_t* bins;           //!< Caller-allocated bin array. Cannot be NULL; set
                                                 //!< bins[0].size to sizeof(astl_discrete_histogram_bin_t).
  uint32_t* bin_count;  //!< In: bin-array capacity (> 0). Out: number of bins written/required.
                        //!< Cannot be NULL.
  uint64_t start_ts;  //!< Filter start timestamp. If non-zero, only samples with timestamp >= this value are included.
                      //!< Uses CLOCK_MONOTONIC_RAW on Linux.
  uint64_t end_ts;  //!< Filter end timestamp. If non-zero, only samples with timestamp <= this value are included. Uses
                    //!< CLOCK_MONOTONIC_RAW on Linux.
} astl_get_metric_discrete_histogram_on_target_params_t;

/**
 * @brief Populate a caller-allocated array with the discrete histogram bins for a metric.
 *
 * This is step 2 of the two-step discrete histogram API. The caller must have previously
 * called astlGetMetricDiscreteHistogramBinCountOnTarget() to obtain @p bin_count and must have allocated
 * an array of at least @p bin_count elements of type astl_discrete_histogram_bin_t.
 *
 * @param params Parameters for this call (see astl_get_metric_discrete_histogram_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlGetMetricDiscreteHistogramOnTarget(
    const astl_get_metric_discrete_histogram_on_target_params_t* params) ASTL_API_NOEXCEPT;

/***********************************************************************************
 **********************     POST-COLLECTION CROPPING      ************************
 **********************************************************************************/

/**
 * @note Common behaviour for all crop APIs (astlCropSamplesOnTarget, astlCropMetricSamplesOnTarget,
 *       astlCropSamples):
 *
 *  - **Retention rule** — a sample is retained if its timestamp falls within at least
 *    one of the supplied [start_ts, end_ts] windows.  All other samples in the scope of the call
 *    are permanently discarded.  Setting @p start_ts to 0 applies no lower bound on the retained range;
 *    setting @p end_ts to 0 applies no upper bound.  When both are non-zero,
 *    @p start_ts must be <= @p end_ts.
 *
 *  - **Precondition** — collection must be stopped on every target within the call's scope before
 *    cropping.  Calling while any affected target is in STARTED or PAUSED state returns
 *    @c ASTL_STATUS_COLLECTION_NOT_STOPPED.
 *
 *  - **Empty result** — if no samples remain after cropping, subsequent data-retrieval APIs
 *    (astlGetCounterSamplesOnTarget, astlGetMetricSamplesOnTarget, astlGetMetricStatisticsOnTarget,
 *    astlGetMetricDiscreteHistogramOnTarget) return @c ASTL_STATUS_NO_DATA_COLLECTED.
 */

/**
 * @brief Describes one time window used by astlCropSamples(), astlCropMetricSamplesOnTarget() and
 * astlCropSamplesOnTarget(). Pass an array of these to the @p windows field of the corresponding params struct.
 */
typedef struct astl_crop_window_t {
  size_t size;        //!< Size of this struct for versioning. Set to sizeof(astl_crop_window_t).
                      //!< Only windows[0].size is checked by the crop APIs.
  uint32_t flags;     //!< Reserved for future flags (must be 0 for now).
  uint64_t start_ts;  //!< Inclusive retention-window start (CLOCK_MONOTONIC_RAW nanoseconds on Linux).
                      //!< Samples with timestamp >= start_ts are candidates for retention.
                      //!< Set to 0 for no lower bound on the retained range.
  uint64_t end_ts;    //!< Inclusive retention-window end (CLOCK_MONOTONIC_RAW nanoseconds on Linux).
                      //!< Samples with timestamp <= end_ts are candidates for retention.
                      //!< Set to 0 for no upper bound on the retained range.
                      //!< Must be >= start_ts when both are non-zero.
} astl_crop_window_t;

/** A parameter structure describes inputs and outputs for astlCropSamplesOnTarget().
 */
typedef struct astl_crop_samples_on_target_params_t {
  size_t size;                              //!< Size of this struct for versioning; set size to
                                            //!< sizeof(astl_crop_samples_on_target_params_t).
  uint32_t                  flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t      target_handle;  //!< Target handle of interest from astl_target_props_t.
  const astl_crop_window_t* windows;        //!< Caller-allocated array of crop windows. Cannot be NULL.
                                            //!< Set windows[0].size to sizeof(astl_crop_window_t).
  uint32_t window_count;                    //!< Number of elements in @p windows. Must be >= 1.
} astl_crop_samples_on_target_params_t;

/**
 * @brief Permanently retain samples for a specific target that fall within one or more time windows, discarding all
 * others.
 *
 * Scope: all counters and metrics collected on the target. See the common-behaviour note above.
 *
 * @param params Parameters for this call (see astl_crop_samples_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlCropSamplesOnTarget(const astl_crop_samples_on_target_params_t* params) ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for astlCropMetricSamplesOnTarget().
 */
typedef struct astl_crop_metric_samples_on_target_params_t {
  size_t size;                              //!< Size of this struct for versioning; set size to
                                            //!< sizeof(astl_crop_metric_samples_on_target_params_t).
  uint32_t                  flags;          //!< Reserved for future flags (must be 0 for now).
  astl_target_handle_t      target_handle;  //!< Target handle of interest from astl_target_props_t.
  astl_metric_handle_t      metric_handle;  //!< Metric handle of interest from astl_metric_props_t.
  const astl_crop_window_t* windows;        //!< Caller-allocated array of crop windows. Cannot be NULL.
                                            //!< Set windows[0].size to sizeof(astl_crop_window_t).
  uint32_t window_count;                    //!< Number of elements in @p windows. Must be >= 1.
} astl_crop_metric_samples_on_target_params_t;

/**
 * @brief Permanently retain samples for a specific metric on a specific target that fall within one or more time
 * windows, discarding all others.
 *
 * Scope: the single (target, metric) pair. See the common-behaviour note above.
 *
 * @param params Parameters for this call (see astl_crop_metric_samples_on_target_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlCropMetricSamplesOnTarget(const astl_crop_metric_samples_on_target_params_t* params)
    ASTL_API_NOEXCEPT;

/** A parameter structure describes inputs and outputs for astlCropSamples().
 */
typedef struct astl_crop_samples_params_t {
  size_t   size;   //!< Size of this struct for versioning; set size to sizeof(astl_crop_samples_params_t).
  uint32_t flags;  //!< Reserved for future flags (must be 0 for now).
  const astl_crop_window_t* windows;  //!< Caller-allocated array of crop windows. Cannot be NULL.
                                      //!< Set windows[0].size to sizeof(astl_crop_window_t).
  uint32_t window_count;              //!< Number of elements in @p windows. Must be >= 1.
} astl_crop_samples_params_t;

/**
 * @brief Permanently retain samples across all targets and metrics that fall within one or more time windows,
 * discarding all others.
 *
 * Scope: every configured target and every collected counter/metric. See the common-behaviour note above.
 *
 * @param params Parameters for this call (see astl_crop_samples_params_t).
 *
 * @return astl_status_code   ASTL_STATUS_SUCCESS on success. Error code otherwise.
 */
ASTL_API astl_status_code astlCropSamples(const astl_crop_samples_params_t* params) ASTL_API_NOEXCEPT;

#if defined(__cplusplus)
}

#endif

#endif  // INCLUDE_ASTL_TELEMETRY_H_
