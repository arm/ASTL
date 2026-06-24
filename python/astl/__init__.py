# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""ASTL Python package initialization.

Exposes a ``__version__`` attribute sourced from installed package metadata.
Falls back to parsing the top-level VERSION.md (when running from a repo
checkout) if distribution metadata is unavailable.
"""

from __future__ import annotations

from importlib import metadata as _metadata
import pathlib as _pl
import re as _re

__all__ = ["__version__"]

def _read_version_from_repo() -> str | None:
    # Walk up two levels (astl/__init__.py -> astl/ -> python/) and then parent.
    current = _pl.Path(__file__).resolve()
    # Repo layout: <root>/python/astl/__init__.py
    for parent in current.parents:
        if (parent / "VERSION.md").is_file():
            try:
                with (parent / "VERSION.md").open("r", encoding="utf-8") as f:
                    for line in f:
                        line = line.strip()
                        if line and not line.startswith("#"):
                            return line
            except OSError:
                return None
    return None

try:  # Prefer distribution metadata (installed environment)
    __version__ = _metadata.version("astl")
    # Clean up helper symbols (no _v in this path)
    del _metadata, _pl, _re, _read_version_from_repo
except _metadata.PackageNotFoundError:  # Editable / source tree fallback
    _v = _read_version_from_repo()
    __version__ = _v if _v is not None else "0.0.0"
    # Cleanup including _v only in this branch
    del _metadata, _pl, _re, _read_version_from_repo, _v
"""ASTL Python bindings high-level package.

This package layers ergonomic Python utilities on top of the generated Cython
bindings (``astl._core``) for the Arm System Telemetry Library (ASTL).

Layered architecture (bottom -> top):
    * _core: direct C API surface (status codes, entities, sample retrieval)
    * exceptions: richer exception hierarchy mapped from status codes
    * streaming / session: convenience polling, async streaming, context managed
        collection configuration & lifecycle handling
    * diagnostics: environment snapshot for debugging installs
    * dataframe / derived: optional analytics helpers for pandas DataFrames and
        simple derived metrics (deltas, rates)


The public API intentionally re-exports frequently used primitives so users can
``import astl`` and access helpers directly (``astl.Session``, ``astl.deltas`` …)
without deep import paths. Advanced users needing tighter control can still
import directly from submodules.

Backward compatibility policy (informal): additive; new helpers may appear but
existing names are not removed without a deprecation period.
"""

from ._core import (
    get_system_info,
    get_targets,
    get_counters,
    get_metrics,
    get_metric_groups,
    get_metric_groups_on_target,
    get_metric_group_metric_count,
    get_metric_group_metrics,
    get_metric_group_metric_count_on_target,
    get_metric_group_metrics_on_target,
    configure_counters,
    configure_counters_on_target,
    configure_metrics,
    configure_metrics_on_target,
    configure_metric_groups,
    configure_metric_groups_on_target,
    start_collection,
    start_collection_paused,
    pause_collection,
    resume_collection,
    stop_collection,
    save_collection,
    load_collection,
    read_immediate,
    get_counter_samples,
    get_metric_samples,
    get_metric_statistics_on_target,
    get_metric_discrete_histogram_on_target,
    get_metric_states_on_target,
    crop_samples,
    crop_samples_on_target,
    crop_metric_samples_on_target,
    MetricStatistics,
    DiscreteHistogramBin,
    MetricState,
    CollectionParameters,
    CollectionMode,
    CollectionParameterFlags,
    ASTLError,
    MetricIdentifier,
    Target,
    version,
    status_name,
    last_status_string,
)
import os as _os, pathlib as _p2, sys as _sys

# Emit a single line identifying the resolved native library path used by this import.
# This aids debugging mismatches or stale copies. Guarded to avoid duplicate noise.
try:  # best-effort; failures are silent
    if not _os.environ.get("ASTL_SUPPRESS_IMPORT_LOG"):
        _pkg_dir = _p2.Path(__file__).resolve().parent
        # Look for the bundled soname (libastl-<MAJOR>.so) next to the package
        _candidates = sorted(_pkg_dir.glob("libastl-*.so"))
        if _candidates:
            print(f"[astl import] native library path: {_candidates[0]}", file=_sys.stderr)
        else:
            print("[astl import] native library path: <not bundled / runtime loader search>", file=_sys.stderr)
finally:
    del _os, _p2, _sys, _pkg_dir, _candidates
from enum import IntEnum
from .streaming import (
    PollResult,
    poll_counter_once,
    poll_metric_once,
    poll_counter_periodic,
    poll_metric_periodic,
    stream_counter,
    stream_metric,
    configure_basic_collection,
)
from .diagnostics import diagnostics, Diagnostics
from .exceptions import (
    InitializationError,
    NotImplementedErrorASTL,
    InvalidArgumentError,
    OutOfMemoryError,
    map_status_to_exception,
    BadArgumentError,
    NotSupportedError,
    DeprecatedAPIError,
    InternalError,
)
from .session import Session
from .dataframe import to_dataframe
from .derived import deltas, rates
# Enum wrappers (lightweight) - could be moved to a separate enums.py if they grow
class Status(IntEnum):
    SUCCESS = 0
    BAD_ARGUMENT = 1
    BAD_CONFIGURATION = 2
    INVALID_TARGET_HANDLE = 3
    INVALID_COUNTER_HANDLE = 4
    INVALID_METRIC_HANDLE = 5
    INVALID_METRIC_GROUP_HANDLE = 6
    NOT_SUPPORTED = 8
    DEPRECATED_API = 9
    NO_TARGET_FOUND = 10
    OLD_STRUCT_VERSION = 11
    NEW_STRUCT_VERSION = 12
    NO_COUNTERS_FOUND = 13
    NO_METRICS_FOUND = 14
    NO_METRIC_GROUPS_FOUND = 15
    BUFFER_TOO_SMALL = 16
    METRIC_RECEIVED_INVALID_SAMPLE = 17
    METRIC_OVERFLOW_DETECTED = 18
    INVALID_SAMPLING_INTERVAL = 19
    SAMPLING_INTERVAL_IGNORED = 20
    INVALID_COLLECTION_MODE = 21
    INVALID_FLAG_VALUE = 22
    COUNTER_NOT_SUPPORTED_ON_TARGET = 23
    METRIC_NOT_SUPPORTED_ON_TARGET = 24
    METRIC_GROUP_NOT_SUPPORTED_ON_TARGET = 25
    COLLECTION_NOT_CONFIGURED = 26
    COLLECTION_NOT_RUNNING = 27
    COLLECTION_NOT_STOPPED = 28
    COLLECTION_NOT_PAUSED = 29
    COLLECTION_ALREADY_RUNNING = 30
    COLLECTION_ALREADY_STOPPED = 31
    COLLECTION_ALREADY_PAUSED = 32
    NO_DATA_COLLECTED = 33
    BUFFER_LARGER_THAN_NEEDED = 34
    UNSUPPORTED_COLLECTOR_TYPE = 35
    FILE_OPEN_FAILED = 36
    FILE_ERROR = 37
    OUT_OF_MEMORY = 38
    INVALID_VALUE_TYPE = 40
    INVALID_STATE_TRANSITION = 41
    PAUSE_UNSUPPORTED = 42
    RESUME_UNSUPPORTED = 43
    INTERNAL_ERROR = 127


class Units(IntEnum):
    UNKNOWN = -1
    NONE = 0
    TICKS = 1
    SECONDS = 2
    CELSIUS = 3
    JOULES = 4
    WATTS = 5
    VOLTS = 6
    AMPS = 7
    BYTES = 8
    MBYTESPERSEC = 9
    MHZ = 10
    RPM = 11
    COUNT = 12
    PERCENT = 13


class ValueType(IntEnum):
    UNKNOWN = -1
    UINT8 = 0
    UINT16 = 1
    UINT32 = 2
    UINT64 = 3
    FLOAT32 = 4
    FLOAT64 = 5
    BOOL8 = 6


class CounterType(IntEnum):
    UNKNOWN = -1
    VALUE = 0
    COUNT = 1
    EVENT = 2


class MetricType(IntEnum):
    UNKNOWN = -1
    VALUE = 0
    FINITE_SET_VALUE = 1
    EVENT = 2
    DELTA = 3
    RESIDENCY = 4
    RATE = 5

__all__ = [
    "initialize",
    "get_system_info",
    "get_targets",
    "get_counters",
    "get_metrics",
    "get_metric_groups",
    "get_metric_groups_on_target",
    "get_metric_group_metric_count",
    "get_metric_group_metrics",
    "get_metric_group_metric_count_on_target",
    "get_metric_group_metrics_on_target",
    "configure_counters",
    "configure_counters_on_target",
    "configure_metrics",
    "configure_metrics_on_target",
    "configure_metric_groups",
    "configure_metric_groups_on_target",
    "start_collection",
    "start_collection_paused",
    "pause_collection",
    "resume_collection",
    "stop_collection",
    "save_collection",
    "load_collection",
    "read_immediate",
    "get_counter_samples",
    "get_metric_samples",
    "get_metric_statistics_on_target",
    "get_metric_discrete_histogram_on_target",
    "get_metric_states_on_target",
    "crop_samples",
    "crop_samples_on_target",
    "crop_metric_samples_on_target",
    "MetricStatistics",
    "DiscreteHistogramBin",
    "MetricState",
    "CollectionParameters",
    "CollectionMode",
    "CollectionParameterFlags",
    "ASTLError",
    "MetricIdentifier",
    "Status",
    "Target",
    "version",
    "status_name",
    "last_status_string",
    "Units",
    "ValueType",
    "CounterType",
    "MetricType",
    "PollResult",
    "poll_counter_once",
    "poll_metric_once",
    "poll_counter_periodic",
    "poll_metric_periodic",
    "stream_counter",
    "stream_metric",
    "configure_basic_collection",
    "diagnostics",
    "Diagnostics",
    "InitializationError",
    "NotImplementedErrorASTL",
    "InvalidArgumentError",
    "OutOfMemoryError",
    "BadArgumentError",
    "NotSupportedError",
    "DeprecatedAPIError",
    "InternalError",
    "map_status_to_exception",
    "Session",
    "to_dataframe",
    "deltas",
    "rates",
]
