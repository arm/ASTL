/**
 * @file astl_telemetry.h
 * @brief Core public telemetry collection C API for the Arm SoC Telemetry Library (ASTL).
 *
 * This header exposes functions to initialize the library, enumerate targets,
 * discover counters/metrics/groups, configure collection parameters (sampling,
 * snapshot, immediate), control collection lifecycle (start / pause / resume /
 * stop / immediate read) and retrieve collected counter & metric samples. All
 * API structs include a leading `_size` field for versioning; callers MUST set
 * this field to `sizeof(struct_type)` before calling into the API so that
 * forward/backward compatibility can be managed. Buffer-returning APIs follow a
 * two-step pattern: query required counts, allocate & initialize (setting the
 * first element's `_size`), then call the getter to populate data.
 */
#ifndef INCLUDE_ASTL_TELEMETRY_H_
#define INCLUDE_ASTL_TELEMETRY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "astl/astl.h"

#if defined(__cplusplus)
extern "C" {
#endif

/***********************************************************************************
 **********************            INITIALIZATION           ************************
 **********************************************************************************/

/**
 * @brief macro to declare a struct of type `type` with name `var` and initialize all fields,
 *        including the _size field for API versioning
 * @example
 * `ASTL_INIT_STRUCT(astl_initialization_parameters_t, init_params, ._config_file_path="~/astl/config.json")
 */
#define ASTL_INIT_STRUCT(type, var, ...) \
  type var { ._size = sizeof(type) __VA_OPT__(, ) __VA_ARGS__ }

/**
 * @brief macro to declare and 0-initialize a `count`-length array of structs of type `type` named `var`
 *        Will initialize the `_size` field of the first element in the array for API versioning
 */
#define ASTL_ALLOC_ARRAY(type, var, count)          \
  type* var = (type*)calloc((count), sizeof(type)); \
  if (var) {                                        \
    var[0]._size = sizeof(type);                    \
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

typedef struct _astl_initialization_parameters_t {
  size_t      _size;                     //!< size of this struct for versioning
  const char* _configuration_file_path;  //!< path to a json astl_configuration  file
} astl_initialization_parameters_t;

/*
 * @brief Re-initializes all internal components of the library, setting up collectors, metrics, etc
 *
 * @param[in] init_params             parameters configuring how the library should be initialized.
 *                                    (placeholder for future expansion)
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlInitialize(const astl_initialization_parameters_t* init_params);

/***********************************************************************************
 **********************               TARGETS               ************************
 **********************************************************************************/

/** A target can be any level in the system where telemetry can be collected. Could be Hardware,
 * firmware, driver, OS or any data source on the system.
 */

typedef const void* astl_target_handle_t;  //!< Abstraction of a target handle

/** A target properties structure describes a target on which telemetry can be collected
 */
typedef struct _astl_target_properties_t {
  size_t               _size;           //!< size of this struct for versioning
  astl_target_handle_t _handle;         //!< Internal handle ot target
  astl_target_handle_t _parent_handle;  //!< Internal handle to the parent device where this target
                                        //!< resides. Null means top level target
  const char* _name;                    //!< Device name
  const char* _description;             //!< Device Description
                                        //!< What other fields? Socket number? Node number?
                                        //!< PCIe BDF? Vendor? Model name? Model number?
                                        //!< Serial number? Version? Unique ID?
} astl_target_properties_t;

/**
 * @brief Get the number of targets on the systems on which telemetry collection can be done
 *
 * @param[in/out] target_count             Number of targets
 *                                         Cannot be NULL. target_count will contain the number of
 *                                         targets. The value should be used to allocate a buffer of
 *                                         astl_target_properties_t big enough to hold
 *                                         target_count_elements
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetTargetCount(uint32_t* target_count);

/**
 * @brief Get properties of all targets on the system on which collection can be done
 *
 * @param[in/out] targets                  Array of target properties. Cannot be NULL. It should
 *                                         point to an allocated buffer of
 *                                         sizeof(aslt_target_properties_t) * (target_count)
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_target_properties_t) for versioning
 *
 * @param[in/out] target_count             The number of elements the targets buffer was allocated
 *                                         for. Returns the number of elements written to the targets
 *                                         buffer.
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetTargets(astl_target_properties_t* targets, uint32_t* target_count);

/***********************************************************************************
 **********************               DATA TYPES               *********************
 **********************************************************************************/

/** Generic units we expect to use. The list does not contain units we are unlikely to encounter
 * while collecting telemetry like liters or kilograms for example
 */
typedef enum _astl_units_t {
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
  ASTL_UNITS_MHERTZ = 10,       //!< Frequency readings in Mhz

  ASTL_UNITS_UNKNOWN = 0xFFFFFFFF,  //!< Unknown units
} astl_units_t;

/** Genetic value types we expect to use.
 */
typedef enum _astl_value_type_t {
  ASTL_VALUE_UINT8   = 0,  //!< Unsigned 8bit integer (char)
  ASTL_VALUE_UINT16  = 1,  //!< Unsigned 16bit integer (short)
  ASTL_VALUE_UINT32  = 2,  //!< Unsigned 32bit integer
  ASTL_VALUE_UINT64  = 3,  //!< Unsigned 64bit integer (long)
  ASTL_VALUE_FLOAT32 = 6,  //!< 32bit float
  ASTL_VALUE_FLOAT64 = 7,  //!< 64bit float (double)
  ASTL_VALUE_BOOL8   = 8,  //!< 8bit boolean
  ASTL_VALUE_STRING  = 9,  //!< String

  ASTL_VALUE_UNKNOWN = 0xFFFFFFFF,  //!< Unknown
} astl_value_type_t;

/** Value container. Processing of an astl_value_t should be based on astl_value_type_t
 * All readings will use this 64bit union to capture any data 64bit in size or less.
 * If there is a reading that is more than 64bit, more than one counter would be used
 * to capture all reading in up to 64bit chunks
 */
typedef union _astl_value_t {
  uint8_t     ui8;   //!< 8bits unsigned integer for UINT8
  uint16_t    ui16;  //!< 16bits unsigned integer for UINT16
  uint32_t    ui32;  //!< 32bits unsigned integer for UINT32
  uint64_t    ui64;  //!< 64bits unsigned integer for UINT64
  float       fp32;  //!< 32bits float for FLOAT32
  double      fp64;  //!< 64bits float for FLAAT64
  bool        b8;    //!< 8bits boolean for BOOL8
  const char* str;   //!< 64bits pointer to string for STRING
} astl_value_t;

/***********************************************************************************
 **********************              COUNTER                   *********************
 **********************************************************************************/

/** A counter contains the most basic raw form of the data. Most of the time, it requires
 * postprocessing to make sense of the data. These raw value containers can be collected directly
 * to minimize post processing overhead or to do different processing with the data elsewhere.
 * It is expected that users of the counter interface would do post precessing of the raw data
 * themselves
 */

typedef const void* astl_counter_handle_t;  //!< Abstraction of a counter handle

/** A counter sample is the raw form of the data we read. It contains both the timestamp when the
 * reading was made and the reading itself
 * Note: The sample is relevant to the target and counter specified in GetSamples API
 */
typedef struct _astl_counter_sample_t {
  size_t _size;  //!< Size of this struct for versioning

  uint64_t     _timestamp;  //!< The timestamp when this value was captured
  astl_value_t _value;      //!< The value captured
} astl_counter_sample_t;

/** The type of the counter. The type helps decide how to process the counter and how to display the
 * processed data
 */
typedef enum _astl_counter_type_t {
  ASTL_COUNTER_TYPE_VALUE = 0,  //!< Point in time value. Example: temperature
  ASTL_COUNTER_TYPE_COUNT = 1,  //!< Free running count. Example: Bytes transferred, joules,
                                //!< time residency
  ASTL_COUNTER_TYPE_EVENT = 2,  //!< Point in time event. Example: Wakeup. Events are traced and
                                //!< cannot be controlled with sampling or immediate reads

  ASTL_COUNTER_TYPE_UNKNOWN = 0xFFFFFFFF,  //!< Unknown
} astl_counter_type_t;

/** A counter properties structure describes a counter
 */
typedef struct _astl_counter_properties_t {
  size_t                _size;                   //!< Size of this struct for versioning
  astl_counter_handle_t _handle;                 //!< The handle of this counter
  const char*           _name;                   //!< The name of this counter
  const char*           _description;            //!< The description of this counter
  uint32_t              _min_sampling_interval;  //!< The minimum sampling interval this counter can be collected
                                                 //!< in ms. Example: 10 means counter cannot be collected
                                                 //!< faster than every 10ms
  astl_units_t _units;                           //!< The raw units of the counter. For example, for memory transfers,
                                                 //!< it would be ASTL_UNIT_BYTES. For temperature, it would be
                                                 //!< ASTL_UNITS_CELSIUS
  uint64_t    _mask;                             //!< Optional: Mask is used to clear out bits in the 64bit container
  const char* _formula;               //!< Transformation required on the counter. Example: & MASK >> 2 DELTA / TIME.
                                      //!< This example would mean: Mask the counter first,
                                      //!<  Then shift the value by 2; do Value2 - Value1 to get a delta and divide the
                                      //!<  result by the elapsed time
  astl_value_type_t _value_type;      //!< The type of the value read from the counter. It is used for
                                      //!< interpreting the 64bit reading.
  astl_counter_type_t _counter_type;  //!< The counter type. It is used for data processing, output
                                      //!< formatting or visualization
} astl_counter_properties_t;

/**
 * @brief Get the number of telemetry counters that can be collected on the specified target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in/out] counter_count            Number of counters
 *                                         Cannot be NULL. counter_count will contain the number of
 *                                         counters. The value should be used to allocate a buffer
 *                                         of astl_counter_properties_t big enough to hold
 *                                         counter_count elements
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetCounterCount(astl_target_handle_t target_handle, uint32_t* counter_count);

/**
 * @brief Get properties of all counters that can be collected on the specified target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in/out] counters                 Array of counter properties structures. Cannot be NULL.
 *                                         It should point to the buffer of
 *                                         sizeof(aslt_counter_properties_t) * (counter_count)
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_counter_properties_t) for versioning
 *
 * @param[in/out] counter_count            The number of elements the counters buffer was allocated
 *                                         for. Returns the number of elements written to the counter
 *                                         buffer.
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetCounters(astl_target_handle_t       target_handle_handle,
                                          astl_counter_properties_t* counters, uint32_t* counter_count);

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

/** A metric sample structure describes a collected and processed value of a metric.
 * It contains both the timestamp when the reading was made and the reading itself
 * Note: The sample is relevant to the target and metric specified in GetSamples API
 */
typedef struct _astl_metric_sample_t {
  size_t       _size;       //!< Size of this struct for versioning
  uint64_t     _timestamp;  //!< the timestamp when this value was captured
  astl_value_t _value;      //!< The value captured. Should use _value_type in the
                            //!< astl_metric_properties_t structure to properly interpret _value
} astl_metric_sample_t;

/** The type of the metric. The type helps decide how to further process, summarize and display metric data
 */
typedef enum _astl_metric_type_t {
  ASTL_METRIC_VALUE            = 0,  //!< Single value in a point in time. Example: Temperature
  ASTL_METRIC_FINITE_SET_VALUE = 1,  //!< Single value from a fixed set of possible values in a
                                     //!< point in time. Idle vs Active
  ASTL_METRIC_EVENT = 2,             //!< non-value event in a point in time. Ex: A wakeup event. Events are
                                     //!< traced and cannot be controlled with sampling or immediate reads
  ASTL_METRIC_DELTA = 3,             //!< diff in value when the counter is of type COUNT.
                                     //!< Example: Dropped packets, energy
  ASTL_METRIC_RESIDENCY = 4,         //!< diff in value when the counter is of type time. Ex: Time spent in
                                     //!< a specific power state
  ASTL_METRIC_RATE = 5,              //!< same as DELTA but over time. Example: Bandwidth as MB/s or
                                     //!< Power as energy/s
  ASTL_METRIC_UNKNOWN = 0xFFFFFFFF,  //!< Unknown
} astl_metric_type_t;

/** A metric properties structure describes a metric
 */
typedef struct _astl_metric_properties_t {
  size_t               _size;                   //!< Size of this struct for versioning
  astl_metric_handle_t _handle;                 //!< This handle of this metric
  const char*          _name;                   //!< The name of this metric
  const char*          _description;            //!< The description of this metric
  uint32_t             _min_sampling_interval;  //!< The minimum sampling interval this metric can be collected
                                                //!< in ms. It is the largest minimum sampling interval
                                                //!< value from all counters used in this metric
  astl_units_t _units;                          //!< The units of the metric. For example, for bandwidth, it would be
                                                //!< ASTL_UNIT_MBYTESPERSEC
  astl_value_type_t _value_type;                //!< The type of the processed value from the metric. Is is used
                                                //!< for interpreting the 64bit metric value
  astl_metric_type_t _metric_type;              //!< The metric type. It is used for output formatting and
                                                //!< visualization
} astl_metric_properties_t;

/**
 * @brief Get the number of telemetry metrics that can be collected on the specified target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in/out] metric_count             Number of metrics
 *                                         Cannot be NULL. metric_count will contain the number of
 *                                         metrics. The value should be used to allocate a buffer
 *                                         of astl_metric_properties_t big enough to hold metric_count
 *                                         elements
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetricCount(astl_target_handle_t target_handle_handle, uint32_t* metric_count);

/**
 * @brief Get properties of all metrics that can be collected on the specified target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in/out] metrics                  Array of metric properties structures. Cannot be NULL.
 *                                         It should point to the buffer of
 *                                         sizeof(aslt_metric_properties_t) * (metric_count)
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_metric_properties_t) for versioning
 *
 * @param[in/out] metric_count             The number of elements the metrics buffer was allocated
 *                                         for. Returns the number of elements written to the metrics
 *                                         buffer.
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetrics(astl_target_handle_t target_handle, astl_metric_properties_t* metric_buffer,
                                         uint32_t* metric_count);

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
typedef struct _astl_metric_group_properties_t {
  size_t                     _size;          //!< Size of this struct for versioning
  astl_metric_group_handle_t _handle;        //!< The handle of this metric group
  const char*                _name;          //!< The name of this metric group
  const char*                _description;   //!< The description of this metric group
  uint32_t                   _metric_count;  //!< The number of metrics in this metric group
  astl_metric_properties_t*  _metrics;       //!< Pointer for buffer of metric properties in this
                                             //!< metric group. User must allocate buffer of
                                             //!< _metric_count elements and call astlGetMetricGroupMetrics
} astl_metric_group_properties_t;

/**
 * @brief Get the number of telemetry metric groups defined for the specified target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in/out] metric_count             Number of metric groups
 *                                         Cannot be NULL. metric_group_count will contain the number
 *                                         of metric groups. The value should be used to allocate a buffer
 *                                         of astl_metric_group_properties_t big enough to hold metric_group_count
 *                                         elements
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetricGroupCount(astl_target_handle_t target_handle, uint32_t* metric_group_count);

/**
 * @brief Get properties of all metric groups defined for the specified target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in/out] metric_groups            Array of metric group properties structures. Cannot be
 *                                         NULL. It should point to the buffer of
 *                                         sizeof(aslt_metric_group_properties_t) * (metric_group_count)
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_metric_group_properties_t) for versioning
 *
 * @param[in/out] metric_group_count       The number of elements the metric groups buffer was
 * allocated for. Returns the number of elements written to the metric groups buffer.
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetricGroups(astl_target_handle_t            target_handle,
                                              astl_metric_group_properties_t* metric_groups,
                                              uint32_t*                       metric_group_count);

/**
 * @brief Get properties of all metrics that can be collected on the specified target that are part
 * of the specified metric group
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 * astl_target_properties_t
 *
 * @param[in/out] metric_group             Pointer to a single metric group structure. Cannot be
 *                                         NULL. Returns the structure with _metrics pointer pointing
 *                                         to populated metrics buffer
 *                                         IMPORTANT: _size field must be set to sizeof(astl_target_properties_t)
 *                                         for versioning
 *
 * @param[in/out] metrics                  Array of metric properties structures. Cannot be NULL.
 *                                         It should point to the buffer of
 *                                         sizeof(aslt_metric_properties_t) * (_metric_count)
 *                                         _metric_count is found in the astl_metric_group_properties_t
 *                                         structure.
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_metric_properties_t) for versioning
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetricGroupMetrics(astl_target_handle_t                  target_handle,
                                                    const astl_metric_group_properties_t* metric_group,
                                                    astl_metric_properties_t*             metrics);

/***********************************************************************************
 **********************              COLLECTION                *********************
 **********************************************************************************/

/* Collection configuration and control section. Users are expected to use an
 * `astl_collection_parameters_t` instance to set the parameters and pass the structure to the
 * configure API. Any subsequent configure collection call using any of the configure API would
 * overwrite previous configurations. Configurations would persist per session: Once configured,
 * multiple start/stop collection calls can be made until data is processed. Collection
 * configurations are per target: Only one of the three ways to collect is possible for a given
 * target at any given time; i.e. Either counter, metric or metric group collection per target
 */

/** The collection mode. The mode configures how and when telemetry data is read
 */
typedef enum _astl_collection_mode_t {
  ASTL_COLLECTION_MODE_SAMPLING  = 0,  //!< Fixed time interval collection mode. Example: every 100ms
  ASTL_COLLECTION_MODE_IMMEDIATE = 1,  //!< Data is captured only when User makes an read API call
                                       //!< for immediate capturing of the data.
  ASTL_COLLECTION_MODE_SNAPSHOT = 2,   //!< Data points are captures when start collection and stop
                                       //!< collection API are called
} astl_collection_mode_t;

/** The collection optimization. Configures the collection to prioritize a specific optimization
 * if possible
 */
typedef enum _astl_collection_optimization_t {
  ASTL_COLLECTION_OPTIMIZATION_OVERHEAD     = 0,  //!< Minimize performance overhead
  ASTL_COLLECTION_OPTIMIZATION_MEMORY       = 1,  //!< Minimize memory usage
  ASTL_COLLECTION_OPTIMIZATION_INTERFERENCE = 2,  //!< Minimize disruption of normal behavior of
                                                  //!< the system
} astl_collection_optimization_t;

/** collection parameter structure describes parameters for the collection
 */
typedef struct _astl_collection_parameters_t {
  size_t   _size;                                //!< Size of this struct for versioning
  uint32_t _sampling_interval;                   //!< optional, used to set the sampling interval in ms if the
                                                 //!< collection mode is set to ASTL_COLLECTION_MODE_SAMPLING.
  astl_collection_mode_t _collection_mode;       //!< SAMPLING, SNAPSHOT, IMMEDIATE.
                                                 //!< Note: Traced events are not configurable;
                                                 //!< they are captured when they happen
  astl_collection_optimization_t _optimization;  //!< (Placeholder for future optimization knobs)
} astl_collection_parameters_t;

/**
 * @brief Configure a counter collection for a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] collection_params            Collection parameters structure
 *                                         IMPORTANT: _size field must be set to sizeof(astl_collection_parameters_t)
 *                                         for versioning
 *
 * @param[in] counter_handles              Array of counter handles to collect. Cannot be NULL. It
 *                                         should point to the buffer of
 *                                         sizeof(aslt_counter_handle_t) * (counter_count)
 *
 * @param[in] counter_count                The number of counters in the buffer of counter handles
 *                                         configured for collection
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlConfigureCounterCollectionOnTarget(astl_target_handle_t                target_handle,
                                                                 const astl_collection_parameters_t* collection_params,
                                                                 const astl_counter_handle_t*        counter_handles,
                                                                 uint32_t                            counter_count);

/**
 * @brief Configure a counter collection for all targets on which the specified counters can be
 * collected
 *
 * @param[in] collection_params            Collection parameters structure
 *                                         IMPORTANT: _size field must be set to sizeof(astl_collection_parameters_t)
 *                                         for versioning
 *
 * @param[in] counter_handles              Array of counter handles to collect. Cannot be NULL. It
 *                                         should point to the buffer of
 *                                         sizeof(aslt_counter_handle_t) * (counter_count)
 *
 * @param[in] counter_count                The number of counters in the buffer of counter handles
 *                                         configured for collection
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlConfigureCounterCollection(const astl_collection_parameters_t* collection_params,
                                                         const astl_counter_handle_t*        counter_handles,
                                                         uint32_t                            counter_count);

/**
 * @brief Configure a metric collection for a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] collection_params            Collection parameters structure
 *                                         IMPORTANT: _size field must be set to sizeof(astl_collection_parameters_t)
 *                                         for versioning
 *
 * @param[in] metric_handles               Array of metric handles to collect. Cannot be NULL. It
 *                                         should point to the buffer of
 *                                         sizeof(aslt_metric_handle_t) * (metric_count)
 *
 * @param[in] metric_count                 The number of metrics in the buffer of metric handles
 *                                         configured for collection
 *
 * @return astl_error_code
 */
astl_status_code ASTL_API astlConfigureMetricCollectionOnTarget(astl_target_handle_t          target_handle,
                                                                astl_collection_parameters_t* collection_params,
                                                                astl_metric_handle_t*         metric_handles,
                                                                uint32_t                      metric_count);

/**
 * @brief Configure a metric collection for all targets on which the specified metrics can be
 * collected
 *
 * @param[in] collection_params            Collection parameters structure
 *                                         IMPORTANT: _size field must be set to sizeof(astl_collection_parameters_t)
 *                                         for versioning
 *
 * @param[in] metric_handles               Array of metric handles to collect. Cannot be NULL. It
 *                                         should point to the buffer of
 *                                         sizeof(aslt_metric_handle_t) * (metric_count)
 *
 * @param[in] metric_count                 The number of metrics in the buffer of metric handles
 *                                         configured for collection
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlConfigureMetricCollection(astl_collection_parameters_t* collection_params,
                                                        astl_metric_handle_t* metric_handles, uint32_t metric_count);

/**
 * @brief Configure a metric group collection for a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] collection_params            Collection parameters structure
 *                                         IMPORTANT: _size field must be set to sizeof(astl_collection_parameters_t)
 *                                         for versioning
 *
 * @param[in] metric_group_handles         Array of metric group handles to collect. Cannot be
 *                                         NULL. It should point to the buffer of
 *                                         sizeof(aslt_metric_group_handle_t) * (metric_group_count)
 *
 * @param[in] metric_group_count           The number of metric groups in the buffer of metric group
 *                                         handles configured for collection
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlConfigureMetricGroupCollectionOnTarget(astl_target_handle_t          target_handle,
                                                                     astl_collection_parameters_t* collection_params,
                                                                     astl_metric_group_handle_t*   metric_group_handles,
                                                                     uint32_t                      metric_group_count);

/**
 * @brief Configure a metric group collection for all targets on which the specified metrics can be
 * collected
 *
 * @param[in] collection_params            Collection parameters structure
 *                                         IMPORTANT: _size field must be set to sizeof(astl_collection_parameters_t)
 *                                         for versioning
 *
 * @param[in] metric_group_handles         Array of metric group handles to collect. Cannot be
 *                                         NULL. It should point to the buffer of
 *                                         sizeof(aslt_metric_group_handle_t) * (metric_group_count)
 *
 * @param[in] metric_group_count           The number of metric groups in the buffer of metric group
 *                                         handles configured for collection
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlConfigureMetricGroupCollection(astl_collection_parameters_t* collection_params,
                                                             astl_metric_group_handle_t*   metric_group_handles,
                                                             uint32_t                      metric_group_count);

/**
 * @brief Do an immediate sample capture of configured counters or metrics on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlReadImmediateOnTarget(astl_target_handle_t target_handle);

/**
 * @brief Do an immediate sample capture of configured counters or metrics on all configured targets
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlReadImmediate();

/**
 * @brief Start telemetry collection on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlStartCollectionOnTarget(astl_target_handle_t target_handle);

/**
 * @brief Start telemetry collection on all targets
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlStartCollection();

/**
 * @brief Pause telemetry collection on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlPauseCollectionOnTarget(astl_target_handle_t target_handle);

/**
 * @brief Pause telemetry collection on all targets
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlPauseCollection();

/**
 * @brief Resume telemetry collection on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlResumeCollectionOnTarget(astl_target_handle_t target_handle);

/**
 * @brief Resume telemetry collection on all targets
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlResumeCollection();

/**
 * @brief Stop telemetry collection on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlStopCollectionOnTarget(astl_target_handle_t target_handle);

/**
 * @brief Stop telemetry collection on all targets
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlStopCollection();

/*** COLLECTED COUNTER SAMPLES ***/
/**
 * @brief Get the number of samples collected for specific counter on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] counter_handle               The handle of the counter of interest. Found in
 *                                         astl_counter_properties_t
 *
 * @param[in/out] sample_count             The number of collected samples
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetCounterSampleCountOnTarget(astl_target_handle_t  target_handle,
                                                            astl_counter_handle_t counter_handle,
                                                            uint32_t*             sample_count);

/**
 * @brief Get the samples collected for specific counter on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] counter_handle               The handle of the counter of interest. Found in
 *                                         astl_counter_properties_t
 *
 * @param[in/out] samples                  Array of collected samples. Cannot be
 *                                         NULL. It should point to the buffer of
 *                                         sizeof(aslt_counter_sample_t) * (sample_count)
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_counter_sample_t)
 *                                         for versioning
 *
 * @param[in/out] sample_count             The number of collected samples
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetCounterSamplesOnTarget(astl_target_handle_t   target_handle,
                                                        astl_counter_handle_t  counter_handle,
                                                        astl_counter_sample_t* samples, uint32_t* sample_count);

/*** COLLECTED METRIC SAMPLES ***/
/**
 * @brief Get the number of samples collected for specific metric on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] metric_handle                The handle of the metric of interest. Found in
 *                                         astl_metric_properties_t
 *
 * @param[in/out] sample_count             The number of collected samples
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetricSampleCountOnTarget(astl_target_handle_t target_handle,
                                                           astl_metric_handle_t metric_handle, uint32_t* sample_count);

/**
 * @brief Get the samples collected for specific metric on a specific target
 *
 * @param[in] target_handle                The handle of the target of interest. Found in
 *                                         astl_target_properties_t
 *
 * @param[in] metric_handle                The handle of the metric of interest. Found in
 *                                         astl_metric_properties_t
 *
 * @param[in/out] samples                  Array of collected samples. Cannot be
 *                                         NULL. It should point to the buffer of
 *                                         sizeof(aslt_metric_sample_t) * (sample_count)
 *                                         IMPORTANT: _size field of at least the first element in the array
 *                                         must be set to sizeof(astl_metric_sample_t)
 *                                         for versioning
 *
 * @param[in/out] sample_count             The number of collected samples
 *
 * @return astl_status_code
 */
ASTL_API astl_status_code astlGetMetricSamplesOnTarget(astl_target_handle_t  target_handle,
                                                       astl_metric_handle_t  metric_handle,
                                                       astl_metric_sample_t* samples, uint32_t* sample_count);

#if defined(__cplusplus)
}
#endif

#endif  // INCLUDE_ASTL_TELEMETRY_H_