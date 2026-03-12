# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

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
        uint32_t major
        uint32_t minor
        uint32_t micro
    ctypedef _astl_version_t astl_version_t
    const char* astlVersionString()
    astl_version_t astlVersion()

cdef extern from "astl/astl_telemetry.h":
    cdef struct _astl_target_props_t:
        size_t size
        const void* handle
        const void* parent_handle
        const char* name
        const char* description
        const char* id
    ctypedef _astl_target_props_t astl_target_props_t

    cdef struct _astl_platform_props_t:
        size_t size
        uint32_t flags
        const char* soc_name
        const char* vendor_id
        const char* os_name
        const char* kernel_name
        const char* kernel_version
        const char* kernel_release
        const char* firmware_version
        const char* hostname
        const char* architecture
    ctypedef _astl_platform_props_t astl_platform_props_t

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
    ctypedef unsigned int astl_units_t

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
    ctypedef unsigned int astl_value_type_t

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
    ctypedef unsigned int astl_counter_type_t

    cdef struct _astl_counter_props_t:
        size_t size
        const void* handle
        const char* name
        const char* description
        uint32_t min_sampling_interval
        astl_units_t units
        const char* formula
        astl_value_type_t value_type
        astl_counter_type_t counter_type
    ctypedef _astl_counter_props_t astl_counter_props_t

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
    ctypedef unsigned int astl_metric_type_t

    cdef enum _astl_category_t:
        ASTL_CATEGORY_COUNT
        ASTL_CATEGORY_TEMPERATURE
        ASTL_CATEGORY_POWER
        ASTL_CATEGORY_FREQUENCY
        ASTL_CATEGORY_VOLTAGE
        ASTL_CATEGORY_CURRENT
        ASTL_CATEGORY_UNCATEGORIZED
    ctypedef unsigned int astl_category_t

    cdef struct _astl_metric_props_t:
        size_t size
        const void* handle
        const char* name
        const char* description
        uint32_t min_sampling_interval
        astl_units_t units
        astl_value_type_t value_type
        astl_metric_type_t metric_type
        astl_category_t category
    ctypedef _astl_metric_props_t astl_metric_props_t

    # Metric Group
    ctypedef const void* astl_metric_group_handle_t
    cdef struct _astl_metric_group_props_t:
        size_t size
        const void* handle
        const char* name
        const char* description
        uint32_t metric_count
        astl_metric_props_t* metrics
    ctypedef _astl_metric_group_props_t astl_metric_group_props_t

    # Metric state discovery
    cdef struct _astl_state_props_t:
        size_t size
        const char* name
        const char* description
        astl_value_t value
    ctypedef _astl_state_props_t astl_state_props_t

    # Collection enums (for future lifecycle exposure)
    cdef enum _astl_collection_mode_t:
        ASTL_COLLECTION_MODE_SAMPLING
        ASTL_COLLECTION_MODE_IMMEDIATE
        ASTL_COLLECTION_MODE_SNAPSHOT
    ctypedef int astl_collection_mode_t

    cdef enum _astl_collection_parameters_flags_t:
        ASTL_COLLECTION_PARAMETERS_FLAG_NONE = 0
        ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD = (1 << 0)
        ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY = (1 << 1)
        ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_INTERFERENCE = (1 << 2)
    ctypedef int astl_collection_parameters_flags_t

    cdef struct _astl_collection_params_t:
        size_t size
        uint32_t flags
        uint32_t sampling_interval
        astl_collection_mode_t collection_mode
    ctypedef _astl_collection_params_t astl_collection_params_t

    # save / load session params
    cdef struct astl_save_params_t:
        size_t size
        uint32_t flags
        const char* output_file_path

    cdef struct astl_load_params_t:
        size_t size
        uint32_t flags
        const char* input_file_path
        size_t chunk_size_bytes

    cdef struct astl_get_system_info_params_t:
        size_t size
        uint32_t flags
        astl_platform_props_t* system_info

    cdef struct astl_get_target_count_params_t:
        size_t size
        uint32_t flags
        uint32_t* target_count

    cdef struct astl_get_targets_params_t:
        size_t size
        uint32_t flags
        astl_target_props_t* targets
        uint32_t* target_count

    cdef struct astl_get_counter_count_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        uint32_t* counter_count

    cdef struct astl_get_counters_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        astl_counter_props_t* counters
        uint32_t* counter_count

    cdef struct astl_get_metric_count_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        uint32_t* metric_count

    cdef struct astl_get_metrics_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        astl_metric_props_t* metrics
        uint32_t* metric_count

    cdef struct astl_get_metric_group_count_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        uint32_t* metric_group_count

    cdef struct astl_get_metric_groups_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        astl_metric_group_props_t* metric_groups
        uint32_t* metric_group_count

    cdef struct astl_get_metric_group_metrics_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const astl_metric_group_props_t* metric_group
        astl_metric_props_t* metrics

    cdef struct astl_get_metric_state_count_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        uint32_t* state_count

    cdef struct astl_get_metric_states_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        astl_state_props_t* states
        uint32_t* state_count

    cdef struct astl_configure_counter_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const astl_collection_params_t* collection_params
        const astl_counter_handle_t* counter_handles
        uint32_t counter_count

    cdef struct astl_configure_metric_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const astl_collection_params_t* collection_params
        const astl_metric_handle_t* metric_handles
        uint32_t metric_count

    cdef struct astl_configure_metric_group_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const astl_collection_params_t* collection_params
        const astl_metric_group_handle_t* metric_group_handles
        uint32_t metric_group_count

    cdef struct astl_read_immediate_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle

    cdef struct astl_read_immediate_params_t:
        size_t size
        uint32_t flags

    cdef struct astl_start_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle

    cdef struct astl_start_collection_params_t:
        size_t size
        uint32_t flags

    cdef struct astl_pause_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle

    cdef struct astl_pause_collection_params_t:
        size_t size
        uint32_t flags

    cdef struct astl_resume_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle

    cdef struct astl_resume_collection_params_t:
        size_t size
        uint32_t flags

    cdef struct astl_stop_collection_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle

    cdef struct astl_stop_collection_params_t:
        size_t size
        uint32_t flags

    # targets
    int astlGetSystemInfo(const astl_get_system_info_params_t* params)
    int astlGetTargetCount(const astl_get_target_count_params_t* params)
    int astlGetTargets(const astl_get_targets_params_t* params)

    # counters
    int astlGetCounterCount(const astl_get_counter_count_params_t* params)
    int astlGetCounters(const astl_get_counters_params_t* params)

    # metrics
    int astlGetMetricCount(const astl_get_metric_count_params_t* params)
    int astlGetMetrics(const astl_get_metrics_params_t* params)

    # metric groups
    int astlGetMetricGroupCount(const astl_get_metric_group_count_params_t* params)
    int astlGetMetricGroups(const astl_get_metric_groups_params_t* params)
    int astlGetMetricGroupMetrics(const astl_get_metric_group_metrics_params_t* params)
    int astlGetMetricStateCountOnTarget(const astl_get_metric_state_count_on_target_params_t* params)
    int astlGetMetricStatesOnTarget(const astl_get_metric_states_on_target_params_t* params)

    # collection configuration
    int astlConfigureCounterCollectionOnTarget(const astl_configure_counter_collection_on_target_params_t* params)
    int astlConfigureMetricCollectionOnTarget(const astl_configure_metric_collection_on_target_params_t* params)
    int astlConfigureMetricGroupCollectionOnTarget(const astl_configure_metric_group_collection_on_target_params_t* params)

    # immediate read
    int astlReadImmediateOnTarget(const astl_read_immediate_on_target_params_t* params)
    int astlReadImmediate(const astl_read_immediate_params_t* params)

    # lifecycle control
    int astlStartCollectionOnTarget(const astl_start_collection_on_target_params_t* params)
    int astlStartCollection(const astl_start_collection_params_t* params)
    int astlPauseCollectionOnTarget(const astl_pause_collection_on_target_params_t* params)
    int astlPauseCollection(const astl_pause_collection_params_t* params)
    int astlResumeCollectionOnTarget(const astl_resume_collection_on_target_params_t* params)
    int astlResumeCollection(const astl_resume_collection_params_t* params)
    int astlStopCollectionOnTarget(const astl_stop_collection_on_target_params_t* params)
    int astlStopCollection(const astl_stop_collection_params_t* params)

    # save/load collection
    int astlSaveCollection(const astl_save_params_t* params)
    int astlLoadCollection(const astl_load_params_t* params)

    # samples
    cdef struct _astl_sample_t:
        uint64_t timestamp
        astl_value_t value
    ctypedef _astl_sample_t astl_sample_t

    cdef struct astl_get_counter_sample_count_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* counter_handle
        uint32_t* sample_count

    cdef struct astl_get_counter_samples_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* counter_handle
        astl_sample_t* samples
        uint32_t* sample_count

    cdef struct astl_get_metric_sample_count_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        uint32_t* sample_count

    cdef struct astl_get_metric_samples_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        astl_sample_t* samples
        uint32_t* sample_count

    int astlGetCounterSampleCountOnTarget(const astl_get_counter_sample_count_on_target_params_t* params)
    int astlGetCounterSamplesOnTarget(const astl_get_counter_samples_on_target_params_t* params)
    int astlGetMetricSampleCountOnTarget(const astl_get_metric_sample_count_on_target_params_t* params)
    int astlGetMetricSamplesOnTarget(const astl_get_metric_samples_on_target_params_t* params)

    # metric summary
    cdef struct _astl_metric_statistics_t:
        size_t   size
        uint32_t flags
        astl_value_t min
        astl_value_t max
        astl_value_t avg
        uint64_t count
    ctypedef _astl_metric_statistics_t astl_metric_statistics_t

    cdef struct astl_get_metric_statistics_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        astl_metric_statistics_t* summary

    int astlGetMetricStatisticsOnTarget(const astl_get_metric_statistics_on_target_params_t* params)

    # discrete histogram
    cdef struct _astl_discrete_histogram_bin_t:
        size_t size
        astl_value_t value
        uint64_t count
    ctypedef _astl_discrete_histogram_bin_t astl_discrete_histogram_bin_t

    cdef struct astl_get_metric_discrete_histogram_bin_count_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        uint32_t* bin_count

    cdef struct astl_get_metric_discrete_histogram_on_target_params_t:
        size_t size
        uint32_t flags
        const void* target_handle
        const void* metric_handle
        astl_discrete_histogram_bin_t* bins
        uint32_t* bin_count

    int astlGetMetricDiscreteHistogramBinCountOnTarget(const astl_get_metric_discrete_histogram_bin_count_on_target_params_t* params)
    int astlGetMetricDiscreteHistogramOnTarget(const astl_get_metric_discrete_histogram_on_target_params_t* params)
