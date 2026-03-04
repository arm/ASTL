# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# distutils: language = c++
# cython: language_level=3

from libc.stdint cimport uint32_t
from libc.stddef cimport size_t
from libc.stdlib cimport calloc, free
from libc.string cimport strdup
from .exceptions import map_status_to_exception

cdef class ASTLError(Exception):
    def __init__(self, code: int):
        self.code = code
        cdef const char* s = astlStatusString(<astl_status_code>code)
        msg = s.decode() if s != NULL else f"ASTL error {code}"
        if code == ASTL_STATUS_NOT_INITIALIZED:
            msg += " ASTL internals not initialized"
        super().__init__(msg)

cdef inline void _check(int code):
    if code != ASTL_STATUS_SUCCESS:
        # Specialized mapping first (may return subclass of ASTLError)
        exc_cls = map_status_to_exception(code)
        if exc_cls is not None:
            raise exc_cls(code)
        if code == ASTL_STATUS_NOT_INITIALIZED:
            raise ASTLError(code)
        raise ASTLError(code)

cdef class Target:
    cdef public object name
    cdef public object description
    cdef public size_t _handle_ptr
    def __init__(self, name: str, description: str, handle_ptr: int):
        # Expose name/description to Python space (tests access target.name)
        self.name = name
        self.description = description
        self._handle_ptr = handle_ptr
    def __repr__(self):
        return f"<Target name={self.name!r}>"

cdef class Counter:
    cdef public object name
    cdef public object description
    cdef public size_t _handle_ptr
    cdef public object min_sampling_interval
    cdef public unsigned int units
    cdef public unsigned int value_type
    cdef public unsigned int counter_type
    cdef public object mask
    cdef public object formula
    def __init__(self, name: str, description: str, handle_ptr: int, min_interval: int, units: int, value_type: int, counter_type: int, mask: int, formula: str):
        self.name = name
        self.description = description
        self._handle_ptr = handle_ptr
        self.min_sampling_interval = min_interval
        self.units = units
        self.value_type = value_type
        self.counter_type = counter_type
        self.mask = mask
        self.formula = formula
    def __repr__(self):
        return f"<Counter name={self.name!r} type={self.counter_type} units={self.units}>"

cdef class Metric:
    cdef public object name
    cdef public object description
    cdef public size_t _handle_ptr
    cdef public object min_sampling_interval
    cdef public unsigned int units
    cdef public unsigned int value_type
    cdef public unsigned int metric_type
    cdef public unsigned int category
    def __init__(self, name: str, description: str, handle_ptr: int, min_interval: int, units: int, value_type: int, metric_type: int, category: int):
        self.name = name
        self.description = description
        self._handle_ptr = handle_ptr
        self.min_sampling_interval = min_interval
        self.units = units
        self.value_type = value_type
        self.metric_type = metric_type
        self.category = category
    def __repr__(self):
        return f"<Metric name={self.name!r} type={self.metric_type} units={self.units} category={self.category}>"

cdef class MetricGroup:
    cdef public object name
    cdef public object description
    cdef public size_t _handle_ptr
    cdef public object metric_count
    def __init__(self, name: str, description: str, handle_ptr: int, metric_count: int):
        self.name = name
        self.description = description
        self._handle_ptr = handle_ptr
        self.metric_count = metric_count
    def __repr__(self):
        return f"<MetricGroup name={self.name!r} metrics={self.metric_count}>"

cdef class MetricState:
    """A state or possible value for a discrete or residency metric.

    For finite-set metrics (ASTL_METRIC_FINITE_SET_VALUE): ``value`` holds the
    decoded enumerated value and ``name`` provides its human-readable label.
    For residency metrics (ASTL_METRIC_RESIDENCY): only ``name`` is meaningful
    (e.g. ``"C6"``, ``"Active"``); ``value`` is ``None``.

    Attributes:
        name (str): State or label name (always populated).
        description (str | None): Optional human-readable description of the state.
        value: Decoded enumerated value for finite-set metrics; ``None`` for
            residency metrics.
    """
    cdef public object name
    cdef public object description
    cdef public object value

    def __init__(self, name: str, description, value):
        self.name = name
        self.description = description
        self.value = value

    def __repr__(self):
        return f"<MetricState name={self.name!r} value={self.value!r}>"

class Category:
    """Namespace of ASTL category codes (mirrors astl_category_t enum)."""
    COUNT = ASTL_CATEGORY_COUNT
    TEMPERATURE = ASTL_CATEGORY_TEMPERATURE
    POWER = ASTL_CATEGORY_POWER
    FREQUENCY = ASTL_CATEGORY_FREQUENCY
    VOLTAGE = ASTL_CATEGORY_VOLTAGE
    CURRENT = ASTL_CATEGORY_CURRENT
    UNCATEGORIZED = ASTL_CATEGORY_UNCATEGORIZED

class Status:
    """Namespace of ASTL status codes (mirrors astl_status_code enum)."""
    SUCCESS = ASTL_STATUS_SUCCESS
    BAD_ARGUMENT = ASTL_STATUS_BAD_ARGUMENT
    BAD_CONFIGURATION = ASTL_STATUS_BAD_CONFIGURATION
    INVALID_TARGET_HANDLE = ASTL_STATUS_INVALID_TARGET_HANDLE
    INVALID_COUNTER_HANDLE = ASTL_STATUS_INVALID_COUNTER_HANDLE
    INVALID_METRIC_HANDLE = ASTL_STATUS_INVALID_METRIC_HANDLE
    INVALID_METRIC_GROUP_HANDLE = ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE
    NOT_IMPLEMENTED = ASTL_STATUS_NOT_IMPLEMENTED
    NOT_SUPPORTED = ASTL_STATUS_NOT_SUPPORTED
    DEPRECATED_API = ASTL_STATUS_DEPRECATED_API
    NO_TARGETS_FOUND = ASTL_STATUS_NO_TARGETS_FOUND
    OLD_TARGET_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION
    NEW_TARGET_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION
    NO_COUNTERS_FOUND = ASTL_STATUS_NO_COUNTERS_FOUND
    OLD_COUNTER_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION
    NEW_COUNTER_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_NEW_COUNTER_PROPERTIES_STRUCT_VERSION
    OLD_COUNTER_SAMPLE_STRUCT_VERSION = ASTL_STATUS_OLD_COUNTER_SAMPLE_STRUCT_VERSION
    NEW_COUNTER_SAMPLE_STRUCT_VERSION = ASTL_STATUS_NEW_COUNTER_SAMPLE_STRUCT_VERSION
    NO_METRICS_FOUND = ASTL_STATUS_NO_METRICS_FOUND
    OLD_METRIC_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION
    NEW_METRIC_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION
    OLD_METRIC_SAMPLE_STRUCT_VERSION = ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION
    NEW_METRIC_SAMPLE_STRUCT_VERSION = ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION
    NO_METRIC_GROUPS_FOUND = ASTL_STATUS_NO_METRIC_GROUPS_FOUND
    OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION
    NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION = ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION
    OLD_COLLECTION_PARAMETERS_STRUCT_VERSION = ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION
    NEW_COLLECTION_PARAMETERS_STRUCT_VERSION = ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION
    TARGET_PROPERTIES_BUFFER_TOO_SMALL = ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL
    COUNTER_PROPERTIES_BUFFER_TOO_SMALL = ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL
    METRIC_PROPERTIES_BUFFER_TOO_SMALL = ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL
    METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL = ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL
    COUNTER_SAMPLES_BUFFER_TOO_SMALL = ASTL_STATUS_COUNTER_SAMPLES_BUFFER_TOO_SMALL
    METRIC_SAMPLES_BUFFER_TOO_SMALL = ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL
    METRIC_RECEIVED_INVALID_SAMPLE = ASTL_STATUS_METRIC_RECEIVED_INVALID_SAMPLE
    METRIC_OVERFLOW_DETECTED = ASTL_STATUS_METRIC_OVERFLOW_DETECTED
    SAMPLING_INTERVAL_TOO_SMALL = ASTL_STATUS_SAMPLING_INTERVAL_TOO_SMALL
    SAMPLING_INTERVAL_TOO_LARGE = ASTL_STATUS_SAMPLING_INTERVAL_TOO_LARGE
    SAMPLING_INTERVAL_IGNORED = ASTL_STATUS_SAMPLING_INTERVAL_IGNORED
    INVALID_COLLECTION_MODE = ASTL_STATUS_INVALID_COLLECTION_MODE
    INVALID_COLLECTION_OPTIMIZATION = ASTL_STATUS_INVALID_COLLECTION_OPTIMIZATION
    COUNTER_NOT_SUPPORTED_ON_TARGET = ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET
    METRIC_NOT_SUPPORTED_ON_TARGET = ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET
    METRIC_GROUP_NOT_SUPPORTED_ON_TARGET = ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET
    COLLECTION_NOT_CONFIGURED = ASTL_STATUS_COLLECTION_NOT_CONFIGURED
    COLLECTION_NOT_RUNNING = ASTL_STATUS_COLLECTION_NOT_RUNNING
    COLLECTION_NOT_STOPPED = ASTL_STATUS_COLLECTION_NOT_STOPPED
    COLLECTION_NOT_PAUSED = ASTL_STATUS_COLLECTION_NOT_PAUSED
    COLLECTION_ALREADY_RUNNING = ASTL_STATUS_COLLECTION_ALREADY_RUNNING
    COLLECTION_ALREADY_STOPPED = ASTL_STATUS_COLLECTION_ALREADY_STOPPED
    COLLECTION_ALREADY_PAUSED = ASTL_STATUS_COLLECTION_ALREADY_PAUSED
    NO_DATA_COLLECTED = ASTL_STATUS_NO_DATA_COLLECTED
    BUFFER_LARGER_THAN_NEEDED = ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED
    UNSUPPORTED_COLLECTOR_TYPE = ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE
    FILE_OPEN_FAILED = ASTL_STATUS_FILE_OPEN_FAILED
    FILE_ERROR = ASTL_STATUS_FILE_ERROR
    OUT_OF_MEMORY = ASTL_STATUS_OUT_OF_MEMORY
    DIVIDE_BY_ZERO = ASTL_STATUS_DIVIDE_BY_ZERO
    INVALID_VALUE_TYPE = ASTL_STATUS_INVALID_VALUE_TYPE
    INCOMPATIBLE_STRUCT_SIZE = ASTL_STATUS_INCOMPATIBLE_STRUCT_SIZE
    NOT_INITIALIZED = ASTL_STATUS_NOT_INITIALIZED
    INTERNAL_ERROR = ASTL_STATUS_INTERNAL_ERROR
    UNKNOWN_ERROR = ASTL_STATUS_UNKNOWN_ERROR

def status_name(code: int) -> str:
    """Return status symbolic name for a numeric code (best-effort)."""
    for k, v in Status.__dict__.items():
        if k.isupper() and isinstance(v, int) and v == code:
            return k
    return f"UNKNOWN({code})"

def version() -> tuple[int, int, int, str]:
    """Return (major, minor, micro, string) for the compiled ASTL library."""
    v = astlVersion()
    return v._major, v._minor, v._micro, astlVersionString().decode()

cpdef dict get_system_info():
    """Return host/session platform metadata from ASTL as a dictionary."""
    cdef astl_platform_properties_t info
    info._size = sizeof(astl_platform_properties_t)
    _check(astlGetSystemInfo(&info))
    return {
        "soc_name": info._soc_name.decode() if info._soc_name != NULL else None,
        "vendor_id": info._vendor_id.decode() if info._vendor_id != NULL else None,
        "os_name": info._os_name.decode() if info._os_name != NULL else None,
        "kernel_name": info._kernel_name.decode() if info._kernel_name != NULL else None,
        "kernel_version": info._kernel_version.decode() if info._kernel_version != NULL else None,
        "kernel_release": info._kernel_release.decode() if info._kernel_release != NULL else None,
        "firmware_version": info._firmware_version.decode() if info._firmware_version != NULL else None,
        "hostname": info._hostname.decode() if info._hostname != NULL else None,
        "architecture": info._architecture.decode() if info._architecture != NULL else None,
    }

cpdef list get_counters(Target target):
    cdef uint32_t count = 0
    _check(astlGetCounterCount(<const void*>target._handle_ptr, &count))
    if count == 0:
        return []
    cdef astl_counter_properties_t* arr = <astl_counter_properties_t*>calloc(count, sizeof(astl_counter_properties_t))
    if arr == NULL:
        raise MemoryError("Failed to allocate counter properties buffer")
    arr[0]._size = sizeof(astl_counter_properties_t)
    try:
        _check(astlGetCounters(<const void*>target._handle_ptr, arr, &count))
        py_list = []
        for i in range(count):
            name = arr[i]._name.decode() if arr[i]._name != NULL else ""
            desc = arr[i]._description.decode() if arr[i]._description != NULL else ""
            formula = arr[i]._formula.decode() if arr[i]._formula != NULL else ""
            py_list.append(Counter(name, desc, <size_t>arr[i]._handle, arr[i]._min_sampling_interval, arr[i]._units, arr[i]._value_type, arr[i]._counter_type, arr[i]._mask, formula))
        return py_list
    finally:
        free(arr)

cpdef list get_metrics(Target target):
    cdef uint32_t count = 0
    cdef int rc = astlGetMetricCount(<const void*>target._handle_ptr, &count)
    if rc in (ASTL_STATUS_NO_METRICS_FOUND, ASTL_STATUS_BAD_ARGUMENT):
        return []
    _check(rc)
    if count == 0:
        return []
    cdef astl_metric_properties_t* arr = <astl_metric_properties_t*>calloc(count, sizeof(astl_metric_properties_t))
    if arr == NULL:
        raise MemoryError("Failed to allocate metric properties buffer")
    arr[0]._size = sizeof(astl_metric_properties_t)
    try:
        rc = astlGetMetrics(<const void*>target._handle_ptr, arr, &count)
        if rc in (ASTL_STATUS_NO_METRICS_FOUND, ASTL_STATUS_BAD_ARGUMENT):
            return []
        _check(rc)
        py_list = []
        for i in range(count):
            name = arr[i]._name.decode() if arr[i]._name != NULL else ""
            desc = arr[i]._description.decode() if arr[i]._description != NULL else ""
            py_list.append(Metric(name, desc, <size_t>arr[i]._handle, arr[i]._min_sampling_interval, arr[i]._units, arr[i]._value_type, arr[i]._metric_type, arr[i]._category))
        return py_list
    finally:
        free(arr)

cpdef list get_metric_groups(Target target):
    cdef uint32_t count = 0
    cdef int rc = astlGetMetricGroupCount(<const void*>target._handle_ptr, &count)
    if rc in (ASTL_STATUS_NO_METRIC_GROUPS_FOUND, ASTL_STATUS_NOT_IMPLEMENTED):
        return []
    _check(rc)
    if count == 0:
        return []
    cdef astl_metric_group_properties_t* arr = <astl_metric_group_properties_t*>calloc(count, sizeof(astl_metric_group_properties_t))
    if arr == NULL:
        raise MemoryError("Failed to allocate metric group properties buffer")
    arr[0]._size = sizeof(astl_metric_group_properties_t)
    try:
        rc = astlGetMetricGroups(<const void*>target._handle_ptr, arr, &count)
        if rc in (ASTL_STATUS_NO_METRIC_GROUPS_FOUND, ASTL_STATUS_NOT_IMPLEMENTED):
            return []
        _check(rc)
        py_list = []
        for i in range(count):
            name = arr[i]._name.decode() if arr[i]._name != NULL else ""
            desc = arr[i]._description.decode() if arr[i]._description != NULL else ""
            py_list.append(MetricGroup(name, desc, <size_t>arr[i]._handle, arr[i]._metric_count))
        return py_list
    finally:
        free(arr)

class CollectionMode:
    SAMPLING = ASTL_COLLECTION_MODE_SAMPLING
    IMMEDIATE = ASTL_COLLECTION_MODE_IMMEDIATE
    SNAPSHOT = ASTL_COLLECTION_MODE_SNAPSHOT

cdef class CollectionParameters:
    cdef public int sampling_interval
    cdef public int mode
    cdef public int optimization

    def __init__(self, sampling_interval: int = 0, mode: int = CollectionMode.IMMEDIATE, optimization: int = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD):
        self.sampling_interval = sampling_interval
        self.mode = mode
        self.optimization = optimization

cdef void _fill_collection_params(CollectionParameters params, astl_collection_parameters_t* p):
    p._size = sizeof(astl_collection_parameters_t)
    p._sampling_interval = <uint32_t>params.sampling_interval
    p._collection_mode = <astl_collection_mode_t>params.mode
    p._optimization = <astl_collection_optimization_t>params.optimization

# --- Configuration helpers ---

cpdef configure_counters_on_target(Target target, params, list counters):
    cdef astl_collection_parameters_t p
    cdef int rc_cc
    _fill_collection_params(params, &p)
    cdef size_t n = len(counters)
    if n == 0:
        raise ValueError("counters list empty")
    cdef size_t i
    cdef const void** handles = <const void**>calloc(n, sizeof(const void*))
    if handles == NULL:
        raise MemoryError()
    try:
        for i in range(n):
            handles[i] = <const void*>counters[i]._handle_ptr
        rc_cc = astlConfigureCounterCollectionOnTarget(<const void*>target._handle_ptr, &p, <const astl_counter_handle_t*>handles, n)
        if rc_cc not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET):
            _check(rc_cc)
    finally:
        free(handles)

cpdef configure_metrics_on_target(Target target, params, list metrics):
    cdef astl_collection_parameters_t p
    cdef int rc_cm
    _fill_collection_params(params, &p)
    cdef size_t n = len(metrics)
    if n == 0:
        raise ValueError("metrics list empty")
    cdef size_t i
    cdef void** handles = <void**>calloc(n, sizeof(void*))
    if handles == NULL:
        raise MemoryError()
    try:
        for i in range(n):
            handles[i] = <void*>metrics[i]._handle_ptr
        rc_cm = astlConfigureMetricCollectionOnTarget(<const void*>target._handle_ptr, &p, <astl_metric_handle_t*>handles, n)
        if rc_cm not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET):
            _check(rc_cm)
    finally:
        free(handles)

cpdef configure_metric_groups_on_target(Target target, params, list groups):
    cdef astl_collection_parameters_t p
    cdef int rc_cg
    _fill_collection_params(params, &p)
    cdef size_t n = len(groups)
    if n == 0:
        raise ValueError("metric groups list empty")
    cdef size_t i
    cdef void** handles = <void**>calloc(n, sizeof(void*))
    if handles == NULL:
        raise MemoryError()
    try:
        for i in range(n):
            handles[i] = <void*>groups[i]._handle_ptr
        rc_cg = astlConfigureMetricGroupCollectionOnTarget(<const void*>target._handle_ptr, &p, <astl_metric_group_handle_t*>handles, n)
        if rc_cg not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET):
            _check(rc_cg)
    finally:
        free(handles)

# --- Lifecycle ---
cpdef start_collection(Target target=None):
    cdef int rc
    if target is None:
        rc = astlStartCollection()
    else:
        rc = astlStartCollectionOnTarget(<const void*>target._handle_ptr)
    if rc not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_BAD_CONFIGURATION, ASTL_STATUS_COLLECTION_NOT_CONFIGURED):
        _check(rc)

cpdef pause_collection(Target target=None):
    cdef int rc
    if target is None:
        rc = astlPauseCollection()
    else:
        rc = astlPauseCollectionOnTarget(<const void*>target._handle_ptr)
    if rc not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_BAD_CONFIGURATION, ASTL_STATUS_COLLECTION_NOT_CONFIGURED, ASTL_STATUS_COLLECTION_NOT_RUNNING):
        _check(rc)

cpdef resume_collection(Target target=None):
    cdef int rc
    if target is None:
        rc = astlResumeCollection()
    else:
        rc = astlResumeCollectionOnTarget(<const void*>target._handle_ptr)
    if rc not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_BAD_CONFIGURATION, ASTL_STATUS_COLLECTION_NOT_CONFIGURED, ASTL_STATUS_COLLECTION_NOT_PAUSED):
        _check(rc)

cpdef stop_collection(Target target=None):
    cdef int rc
    if target is None:
        rc = astlStopCollection()
    else:
        rc = astlStopCollectionOnTarget(<const void*>target._handle_ptr)
    if rc not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_BAD_CONFIGURATION, ASTL_STATUS_COLLECTION_NOT_CONFIGURED, ASTL_STATUS_COLLECTION_NOT_RUNNING):
        _check(rc)

cpdef save_collection(output_file_path=None):
    """Save current ASTL session state.

    Args:
        output_file_path: Required path to output `.astl` archive (non-empty string).
    """
    cdef astl_save_params_t params
    cdef bytes encoded_path

    if output_file_path is None or output_file_path == "":
        raise ValueError("output_file_path must be a non-empty string")
    if not isinstance(output_file_path, str):
        raise TypeError("output_file_path must be str")

    params._size = sizeof(astl_save_params_t)
    params._flags = 0
    encoded_path = (<str>output_file_path).encode()
    params._output_file_path = encoded_path

    _check(astlSaveCollection(&params))

cpdef load_collection(input_file_path, chunk_size_bytes: int = 0):
    """Load a previously saved ASTL session archive.

    Args:
        input_file_path: Path to `.astl` archive to load (required, non-empty).
        chunk_size_bytes: Reserved loader chunk size forwarded to the C API.
    """
    cdef astl_load_params_t params
    cdef bytes encoded_path

    if input_file_path is None or input_file_path == "":
        raise ValueError("input_file_path must be a non-empty string")
    if not isinstance(input_file_path, str):
        raise TypeError("input_file_path must be str")
    if chunk_size_bytes < 0:
        raise ValueError("chunk_size_bytes must be >= 0")

    encoded_path = (<str>input_file_path).encode()

    params._size = sizeof(astl_load_params_t)
    params._input_file_path = encoded_path
    params._chunk_size_bytes = <size_t>chunk_size_bytes
    params._flags = 0

    _check(astlLoadCollection(&params))

cpdef read_immediate(Target target=None):
    cdef int rc
    if target is None:
        rc = astlReadImmediate()
    else:
        rc = astlReadImmediateOnTarget(<const void*>target._handle_ptr)
    # Treat NOT_IMPLEMENTED, BAD_CONFIGURATION, and COLLECTION_NOT_CONFIGURED as benign (mirrors lifecycle tolerance)
    if rc not in (ASTL_STATUS_NOT_IMPLEMENTED, ASTL_STATUS_BAD_CONFIGURATION, ASTL_STATUS_COLLECTION_NOT_CONFIGURED):
        _check(rc)

# --- Sample retrieval ---

cpdef list get_counter_samples(Target target, Counter counter):
    cdef uint32_t count = 0
    _check(astlGetCounterSampleCountOnTarget(<const void*>target._handle_ptr, <const void*>counter._handle_ptr, &count))
    if count == 0:
        return []
    cdef astl_counter_sample_t* arr = <astl_counter_sample_t*>calloc(count, sizeof(astl_counter_sample_t))
    if arr == NULL:
        raise MemoryError()
    arr[0]._size = sizeof(astl_counter_sample_t)
    try:
        _check(astlGetCounterSamplesOnTarget(<const void*>target._handle_ptr, <const void*>counter._handle_ptr, arr, &count))
        out = []
        for i in range(count):
            out.append((arr[i]._timestamp, _decode_value(counter.value_type, arr[i]._value)))
        return out
    finally:
        free(arr)

cpdef list get_metric_samples(Target target, Metric metric):
    cdef uint32_t count = 0
    _check(astlGetMetricSampleCountOnTarget(<const void*>target._handle_ptr, <const void*>metric._handle_ptr, &count))
    if count == 0:
        return []
    cdef astl_metric_sample_t* arr = <astl_metric_sample_t*>calloc(count, sizeof(astl_metric_sample_t))
    if arr == NULL:
        raise MemoryError()
    arr[0]._size = sizeof(astl_metric_sample_t)
    try:
        _check(astlGetMetricSamplesOnTarget(<const void*>target._handle_ptr, <const void*>metric._handle_ptr, arr, &count))
        out = []
        for i in range(count):
            out.append((arr[i]._timestamp, _decode_value(metric.value_type, arr[i]._value)))
        return out
    finally:
        free(arr)

cdef object _decode_value(int value_type, astl_value_t v):
    if value_type == ASTL_VALUE_UINT8:
        return v.ui8
    elif value_type == ASTL_VALUE_UINT16:
        return v.ui16
    elif value_type == ASTL_VALUE_UINT32:
        return v.ui32
    elif value_type == ASTL_VALUE_UINT64:
        return v.ui64
    elif value_type == ASTL_VALUE_FLOAT32:
        return v.fp32
    elif value_type == ASTL_VALUE_FLOAT64:
        return v.fp64
    elif value_type == ASTL_VALUE_BOOL8:
        return bool(v.b8)
    else:
        return None

cdef class MetricStatistics:
    """Min/max/average summary for a collected metric on a target.

    Attributes:
        count (int): Number of samples processed. If 0, no samples were
            available and ``min``/``max``/``avg`` should not be used.
        min: Minimum sampled value (type matches the metric's value_type).
        max: Maximum sampled value.
        avg (float): Average of all sampled values.
    """
    cdef public uint64_t count
    cdef public object   min
    cdef public object   max
    cdef public object   avg

    def __init__(self, count: int, min, max, avg):
        self.count = count
        self.min   = min
        self.max   = max
        self.avg   = avg

    def __repr__(self):
        return (f"<MetricStatistics count={self.count} "
                f"min={self.min!r} max={self.max!r} avg={self.avg!r}>")


cpdef MetricStatistics get_metric_statistics_on_target(Target target, Metric metric):
    """Return a min/max/average summary for ``metric`` on ``target``.

    Args:
        target: The :class:`Target` the metric was collected on.
        metric: The :class:`Metric` to summarise.

    Returns:
        A :class:`MetricStatistics` with ``count``, ``min``, ``max``, and
        ``avg`` fields.  When ``count`` is 0 no samples were collected and
        the ``min``/``max``/``avg`` fields are ``None``.

    Raises:
        ASTLError: on any non-success status code (e.g. NOT_SUPPORTED for
            non-arithmetic metric types such as BOOL8 or STRING).
    """
    cdef astl_metric_statistics_t s
    s._size  = sizeof(astl_metric_statistics_t)
    s._flags = 0
    s._count = 0
    _check(astlGetMetricStatisticsOnTarget(
        <const void*>target._handle_ptr,
        <const void*>metric._handle_ptr,
        &s,
    ))
    if s._count == 0:
        return MetricStatistics(0, None, None, None)
    cdef object vmin = _decode_value(metric.value_type, s._min)
    cdef object vmax = _decode_value(metric.value_type, s._max)
    # Average is always fp64 regardless of the metric's value_type
    cdef object vavg = s._avg.fp64
    return MetricStatistics(s._count, vmin, vmax, vavg)


cdef class DiscreteHistogramBin:
    """A single bin in a discrete histogram.

    Attributes:
        value: The exact sampled value for this bin (type matches the
            metric's value_type).
        count (int): Number of samples that had exactly this value.
    """
    cdef public object   value
    cdef public uint32_t count

    def __init__(self, value, count: int):
        self.value = value
        self.count = count

    def __repr__(self):
        return f"<DiscreteHistogramBin value={self.value!r} count={self.count}>"


cpdef list get_metric_discrete_histogram_on_target(Target target, Metric metric):
    """Return the discrete histogram bins for ``metric`` on ``target``.

    Uses the two-step C API: first queries the bin count, then allocates and
    fills the bin array.

    Args:
        target: The :class:`Target` the metric was collected on.
        metric: The :class:`Metric` to histogram.

    Returns:
        A list of :class:`DiscreteHistogramBin` objects, one per unique
        sampled value.  Returns an empty list when no samples were collected.

    Raises:
        ASTLError or a subclass (e.g. NotSupportedError): on any non-success
            status code (e.g. NOT_SUPPORTED for metric types not supported by
            the discrete histogram summarizer).
    """
    cdef uint32_t bin_count = 0
    _check(astlGetMetricDiscreteHistogramBinCountOnTarget(
        <const void*>target._handle_ptr,
        <const void*>metric._handle_ptr,
        &bin_count,
    ))
    if bin_count == 0:
        return []
    cdef astl_discrete_histogram_bin_t* bins = \
        <astl_discrete_histogram_bin_t*>calloc(bin_count, sizeof(astl_discrete_histogram_bin_t))
    if bins == NULL:
        raise MemoryError()
    bins[0]._size = sizeof(astl_discrete_histogram_bin_t)
    try:
        _check(astlGetMetricDiscreteHistogramOnTarget(
            <const void*>target._handle_ptr,
            <const void*>metric._handle_ptr,
            bins,
            &bin_count,
        ))
        out = []
        for i in range(bin_count):
            out.append(DiscreteHistogramBin(
                _decode_value(metric.value_type, bins[i]._value),
                bins[i]._count,
            ))
        return out
    finally:
        free(bins)


cpdef list get_metric_states_on_target(Target target, Metric metric):
    """Return the state descriptors for a finite-set or residency metric.

    Uses the two-step C API: first queries the state count via
    ``astlGetMetricStateCountOnTarget``, then allocates and fills the state
    array via ``astlGetMetricStatesOnTarget``.

    Args:
        target: The :class:`Target` to query.
        metric: The :class:`Metric` to query. Must be of type
            ``ASTL_METRIC_FINITE_SET_VALUE`` or ``ASTL_METRIC_RESIDENCY``.

    Returns:
        A list of :class:`MetricState` objects.  For finite-set metrics each
        state carries both a decoded ``value`` and a ``name`` label.  For
        residency metrics only ``name`` is meaningfully populated; ``value``
        is ``None``.  Returns an empty list when the metric has no states.

    Raises:
        ASTLError / NotSupportedError: when the metric type is neither
            ASTL_METRIC_FINITE_SET_VALUE nor ASTL_METRIC_RESIDENCY.
    """
    cdef uint32_t count = 0
    _check(astlGetMetricStateCountOnTarget(
        <const void*>target._handle_ptr,
        <const void*>metric._handle_ptr,
        &count,
    ))
    if count == 0:
        return []
    cdef astl_state_properties_t* arr = \
        <astl_state_properties_t*>calloc(count, sizeof(astl_state_properties_t))
    if arr == NULL:
        raise MemoryError()
    arr[0]._size = sizeof(astl_state_properties_t)
    try:
        _check(astlGetMetricStatesOnTarget(
            <const void*>target._handle_ptr,
            <const void*>metric._handle_ptr,
            arr,
            &count,
        ))
        is_finite_set = metric.metric_type == ASTL_METRIC_FINITE_SET_VALUE
        out = []
        for i in range(count):
            name = arr[i]._name.decode() if arr[i]._name != NULL else ""
            desc = arr[i]._description.decode() if arr[i]._description != NULL else ""
            value = _decode_value(metric.value_type, arr[i]._value) if is_finite_set else None
            out.append(MetricState(name, desc, value))
        return out
    finally:
        free(arr)


cpdef list get_targets():
    cdef uint32_t count = 0
    _check(astlGetTargetCount(&count))
    if count == 0:
        return []
    cdef astl_target_properties_t* arr = <astl_target_properties_t*>calloc(count, sizeof(astl_target_properties_t))
    if arr == NULL:
        raise MemoryError("Failed to allocate target properties buffer")
    arr[0]._size = sizeof(astl_target_properties_t)
    try:
        _check(astlGetTargets(arr, &count))
        py_list = []
        for i in range(count):
            name = arr[i]._name.decode() if arr[i]._name != NULL else ""
            desc = arr[i]._description.decode() if arr[i]._description != NULL else ""
            py_list.append(Target(name, desc, <size_t>arr[i]._handle))
        return py_list
    finally:
        free(arr)
