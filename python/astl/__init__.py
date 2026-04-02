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
    ASTLError,
    MetricIdentifier,
    Status,
    Target,
    version,
    status_name,
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
class Units(IntEnum):
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
    MHERTZ = 10
    UNKNOWN = 11


class ValueType(IntEnum):
    UINT8 = 0
    UINT16 = 1
    UINT32 = 2
    UINT64 = 3
    FLOAT32 = 4
    FLOAT64 = 5
    BOOL8 = 6
    STRING = 7
    UNKNOWN = 8


class CounterType(IntEnum):
    VALUE = 0
    COUNT = 1
    EVENT = 2
    UNKNOWN = 3


class MetricType(IntEnum):
    VALUE = 0
    FINITE_SET_VALUE = 1
    EVENT = 2
    DELTA = 3
    RESIDENCY = 4
    RATE = 5
    UNKNOWN = 6

__all__ = [
    "initialize",
    "get_system_info",
    "get_targets",
    "get_counters",
    "get_metrics",
    "get_metric_groups",
    "get_metric_groups_on_target",
    "get_metric_group_metric_count",
    "get_metric_group_metric_count_on_target",
    "get_metric_group_metrics",
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
    "ASTLError",
    "MetricIdentifier",
    "Status",
    "Target",
    "version",
    "status_name",
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
