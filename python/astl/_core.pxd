"""Cython declarations for a subset of the ASTL C API."""

from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libc.stddef cimport size_t
from libc.stdlib cimport calloc, free

cdef extern from "astl/astl_errors.h":
    cdef enum _astl_status_code:
        ASTL_STATUS_SUCCESS
        ASTL_STATUS_BAD_ARGUMENT
        ASTL_STATUS_BAD_CONFIGURATION
        ASTL_STATUS_INVALID_TARGET_HANDLE
        ASTL_STATUS_INVALID_COUNTER_HANDLE
        ASTL_STATUS_INVALID_METRIC_HANDLE
        ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE
        ASTL_STATUS_NOT_IMPLEMENTED
        ASTL_STATUS_NOT_SUPPORTED
        ASTL_STATUS_DEPRECATED_API
        ASTL_STATUS_NO_TARGETS_FOUND
        ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_NO_COUNTERS_FOUND
        ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_NEW_COUNTER_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_OLD_COUNTER_SAMPLE_STRUCT_VERSION
        ASTL_STATUS_NEW_COUNTER_SAMPLE_STRUCT_VERSION
        ASTL_STATUS_NO_METRICS_FOUND
        ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION
        ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION
        ASTL_STATUS_NO_METRIC_GROUPS_FOUND
        ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION
        ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION
        ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION
        ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL
        ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL
        ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL
        ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL
        ASTL_STATUS_COUNTER_SAMPLES_BUFFER_TOO_SMALL
        ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL
        ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE
        ASTL_STATUS_METRIC_OVERFLOW_DETECTED
        ASTL_STATUS_SAMPLING_INTERVAL_TOO_SMALL
        ASTL_STATUS_SAMPLING_INTERVAL_TOO_LARGE
        ASTL_STATUS_SAMPLING_INTERVAL_IGNORED
        ASTL_STATUS_INVALID_COLLECTION_MODE
        ASTL_STATUS_INVALID_COLLECTION_OPTIMIZATION
        ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET
        ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET
        ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED
        ASTL_STATUS_COLLECTION_NOT_RUNNING
        ASTL_STATUS_COLLECTION_NOT_STOPPED
        ASTL_STATUS_COLLECTION_NOT_PAUSED
        ASTL_STATUS_COLLECTION_ALREADY_RUNNING
        ASTL_STATUS_COLLECTION_ALREADY_STOPPED
        ASTL_STATUS_COLLECTION_ALREADY_PAUSED
        ASTL_STATUS_NO_DATA_COLLECTED
        ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
        ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE
        ASTL_STATUS_FILE_OPEN_FAILED
        ASTL_STATUS_FILE_ERROR
        ASTL_STATUS_OUT_OF_MEMORY
        ASTL_STATUS_DIVIDE_BY_ZERO
        ASTL_STATUS_INVALID_VALUE_TYPE
        ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE
        ASTL_STATUS_NOT_INITIALIZED
        ASTL_STATUS_INTERNAL_ERROR
        ASTL_STATUS_UNKNOWN_ERROR
    ctypedef int astl_status_code
    const char* astlStatusString(astl_status_code status)

cdef extern from "astl/astl_version.h":
    cdef struct _astl_version_t:
        uint32_t _major
        uint32_t _minor
        uint32_t _micro
    ctypedef _astl_version_t astl_version_t
    const char* astlVersionString()
    astl_version_t astlVersion()

cdef extern from "astl/astl_telemetry.h":
    cdef struct _astl_initialization_parameters_t:
        size_t _size
        const char* _configuration_file_path
    ctypedef _astl_initialization_parameters_t astl_initialization_parameters_t

    cdef struct _astl_target_properties_t:
        size_t _size
        const void* _handle
        const void* _parent_handle
        const char* _name
        const char* _description
    ctypedef _astl_target_properties_t astl_target_properties_t

    cdef struct _astl_platform_properties_t:
        size_t _size
        const char* _soc_name
        const char* _vendor_id
        const char* _os_name
        const char* _kernel_name
        const char* _kernel_version
        const char* _kernel_release
        const char* _firmware_version
        const char* _hostname
        const char* _architecture
    ctypedef _astl_platform_properties_t astl_platform_properties_t

    # Units
    cdef enum _astl_units_t:
        ASTL_UNITS_NONE
        ASTL_UNITS_TICKS
        ASTL_UNITS_SECONDS
        ASTL_UNITS_CELSIUS
        ASTL_UNITS_JOULES
        ASTL_UNITS_WATTS
        ASTL_UNITS_VOLTS
        ASTL_UNITS_AMPS
        ASTL_UNITS_BYTES
        ASTL_UNITS_MBYTESPERSEC
        ASTL_UNITS_MHERTZ
        ASTL_UNITS_UNKNOWN
    ctypedef int astl_units_t

    # Value types
    cdef enum _astl_value_type_t:
        ASTL_VALUE_UINT8
        ASTL_VALUE_UINT16
        ASTL_VALUE_UINT32
        ASTL_VALUE_UINT64
        ASTL_VALUE_FLOAT32
        ASTL_VALUE_FLOAT64
        ASTL_VALUE_BOOL8
        ASTL_VALUE_UNKNOWN
    ctypedef int astl_value_type_t

    cdef struct _astl_value_t:
        uint8_t ui8
        uint16_t ui16
        uint32_t ui32
        uint64_t ui64
        float fp32
        double fp64
        bint b8
    ctypedef _astl_value_t astl_value_t

    # Counter
    ctypedef const void* astl_counter_handle_t
    cdef enum _astl_counter_type_t:
        ASTL_COUNTER_TYPE_VALUE
        ASTL_COUNTER_TYPE_COUNT
        ASTL_COUNTER_TYPE_EVENT
        ASTL_COUNTER_TYPE_UNKNOWN
    ctypedef int astl_counter_type_t

    cdef struct _astl_counter_properties_t:
        size_t _size
        const void* _handle
        const char* _name
        const char* _description
        uint32_t _min_sampling_interval
        astl_units_t _units
        uint64_t _mask
        const char* _formula
        astl_value_type_t _value_type
        astl_counter_type_t _counter_type
    ctypedef _astl_counter_properties_t astl_counter_properties_t

    # Metric
    ctypedef const void* astl_metric_handle_t
    cdef enum _astl_metric_type_t:
        ASTL_METRIC_VALUE
        ASTL_METRIC_FINITE_SET_VALUE
        ASTL_METRIC_EVENT
        ASTL_METRIC_DELTA
        ASTL_METRIC_RESIDENCY
        ASTL_METRIC_RATE
        ASTL_METRIC_UNKNOWN
    ctypedef int astl_metric_type_t

    cdef enum _astl_category_t:
        ASTL_CATEGORY_COUNT
        ASTL_CATEGORY_TEMPERATURE
        ASTL_CATEGORY_POWER
        ASTL_CATEGORY_FREQUENCY
        ASTL_CATEGORY_VOLTAGE
        ASTL_CATEGORY_CURRENT
        ASTL_CATEGORY_UNCATEGORIZED
    ctypedef int astl_category_t

    cdef struct _astl_metric_properties_t:
        size_t _size
        const void* _handle
        const char* _name
        const char* _description
        uint32_t _min_sampling_interval
        astl_units_t _units
        astl_value_type_t _value_type
        astl_metric_type_t _metric_type
        astl_category_t _category
    ctypedef _astl_metric_properties_t astl_metric_properties_t

    # Metric Group
    ctypedef const void* astl_metric_group_handle_t
    cdef struct _astl_metric_group_properties_t:
        size_t _size
        const void* _handle
        const char* _name
        const char* _description
        uint32_t _metric_count
        astl_metric_properties_t* _metrics
    ctypedef _astl_metric_group_properties_t astl_metric_group_properties_t

    # Collection enums (for future lifecycle exposure)
    cdef enum _astl_collection_mode_t:
        ASTL_COLLECTION_MODE_SAMPLING
        ASTL_COLLECTION_MODE_IMMEDIATE
        ASTL_COLLECTION_MODE_SNAPSHOT
    ctypedef int astl_collection_mode_t

    cdef enum _astl_collection_optimization_t:
        ASTL_COLLECTION_OPTIMIZATION_OVERHEAD
        ASTL_COLLECTION_OPTIMIZATION_MEMORY
        ASTL_COLLECTION_OPTIMIZATION_INTERFERENCE
    ctypedef int astl_collection_optimization_t

    cdef struct _astl_collection_parameters_t:
        size_t _size
        uint32_t _sampling_interval
        astl_collection_mode_t _collection_mode
        astl_collection_optimization_t _optimization
    ctypedef _astl_collection_parameters_t astl_collection_parameters_t

    # save / load session params
    cdef struct astl_save_params_t:
        size_t _size
        const char* _output_file_path
        uint32_t _flags

    cdef struct astl_load_params_t:
        size_t _size
        const char* _input_file_path
        size_t _chunk_size_bytes
        uint32_t _flags

    # targets
    int astlGetSystemInfo(astl_platform_properties_t* system_info)
    int astlGetTargetCount(uint32_t* target_count)
    int astlGetTargets(astl_target_properties_t* targets, uint32_t* target_count)

    # counters
    int astlGetCounterCount(const void* target_handle, uint32_t* counter_count)
    int astlGetCounters(const void* target_handle, astl_counter_properties_t* counters, uint32_t* counter_count)

    # metrics
    int astlGetMetricCount(const void* target_handle, uint32_t* metric_count)
    int astlGetMetrics(const void* target_handle, astl_metric_properties_t* metrics, uint32_t* metric_count)

    # metric groups
    int astlGetMetricGroupCount(const void* target_handle, uint32_t* metric_group_count)
    int astlGetMetricGroups(const void* target_handle, astl_metric_group_properties_t* groups, uint32_t* group_count)
    int astlGetMetricGroupMetrics(const void* target_handle, const astl_metric_group_properties_t* group, astl_metric_properties_t* metrics)

    # collection configuration
    int astlConfigureCounterCollectionOnTarget(const void* target_handle, const astl_collection_parameters_t* params, const astl_counter_handle_t* counter_handles, uint32_t counter_count)
    int astlConfigureCounterCollection(const astl_collection_parameters_t* params, const astl_counter_handle_t* counter_handles, uint32_t counter_count)
    int astlConfigureMetricCollectionOnTarget(const void* target_handle, astl_collection_parameters_t* params, astl_metric_handle_t* metric_handles, uint32_t metric_count)
    int astlConfigureMetricCollection(astl_collection_parameters_t* params, astl_metric_handle_t* metric_handles, uint32_t metric_count)
    int astlConfigureMetricGroupCollectionOnTarget(const void* target_handle, astl_collection_parameters_t* params, astl_metric_group_handle_t* group_handles, uint32_t group_count)
    int astlConfigureMetricGroupCollection(astl_collection_parameters_t* params, astl_metric_group_handle_t* group_handles, uint32_t group_count)

    # immediate read
    int astlReadImmediateOnTarget(const void* target_handle)
    int astlReadImmediate()

    # lifecycle control
    int astlStartCollectionOnTarget(const void* target_handle)
    int astlStartCollection()
    int astlPauseCollectionOnTarget(const void* target_handle)
    int astlPauseCollection()
    int astlResumeCollectionOnTarget(const void* target_handle)
    int astlResumeCollection()
    int astlStopCollectionOnTarget(const void* target_handle)
    int astlStopCollection()

    # save/load collection
    int astlSaveCollection(const astl_save_params_t* params)
    int astlLoadCollection(const astl_load_params_t* params)

    # samples
    cdef struct _astl_counter_sample_t:
        size_t _size
        uint64_t _timestamp
        astl_value_t _value
    ctypedef _astl_counter_sample_t astl_counter_sample_t

    cdef struct _astl_metric_sample_t:
        size_t _size
        uint64_t _timestamp
        astl_value_t _value
    ctypedef _astl_metric_sample_t astl_metric_sample_t

    int astlGetCounterSampleCountOnTarget(const void* target_handle, const void* counter_handle, uint32_t* sample_count)
    int astlGetCounterSamplesOnTarget(const void* target_handle, const void* counter_handle, astl_counter_sample_t* samples, uint32_t* sample_count)
    int astlGetMetricSampleCountOnTarget(const void* target_handle, const void* metric_handle, uint32_t* sample_count)
    int astlGetMetricSamplesOnTarget(const void* target_handle, const void* metric_handle, astl_metric_sample_t* samples, uint32_t* sample_count)

    # metric summary
    cdef struct _astl_metric_statistics_t:
        size_t   _size
        astl_value_t _min
        astl_value_t _max
        astl_value_t _avg
        uint64_t _count
        uint32_t _flags
    ctypedef _astl_metric_statistics_t astl_metric_statistics_t

    int astlGetMetricStatistics(const void* target_handle, const void* metric_handle, astl_metric_statistics_t* summary)
