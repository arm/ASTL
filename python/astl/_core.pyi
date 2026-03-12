# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Stub file for mypy type checking (minimal surface)
from typing import List, Tuple, Any

class ASTLError(Exception): ...

class Target:
    name: str
    description: str
    handle_ptr: int

class Counter:
    name: str
    description: str
    handle_ptr: int
    min_sampling_interval: int
    units: int
    value_type: int
    counter_type: int
    formula: str

class Metric:
    name: str
    description: str
    handle_ptr: int
    min_sampling_interval: int
    units: int
    value_type: int
    metric_type: int
    category: int

class MetricGroup:
    name: str
    description: str
    handle_ptr: int
    metric_count: int

class CollectionParameters:
    sampling_interval: int
    mode: int
    flags: int
    def __init__(self, sampling_interval: int = ..., mode: int = ..., flags: int = ...) -> None: ...

class CollectionMode:  # minimal enum-like stub
    IMMEDIATE: int
    SAMPLING: int
    SNAPSHOT: int

class Category:
    COUNT: int
    TEMPERATURE: int
    POWER: int
    FREQUENCY: int
    VOLTAGE: int
    CURRENT: int
    UNCATEGORIZED: int

class Status:
    SUCCESS: int

# Functions

def get_system_info() -> dict[str, str | None]: ...

def get_targets() -> List[Target]: ...

def get_counters(target: Target) -> List[Counter]: ...

def get_metrics(target: Target) -> List[Metric]: ...

def get_metric_groups(target: Target) -> List[MetricGroup]: ...

# Configuration helpers
def configure_counters_on_target(target: Target, params: CollectionParameters, counters: List[Counter]) -> None: ...
def configure_metrics_on_target(target: Target, params: CollectionParameters, metrics: List[Metric]) -> None: ...
def configure_metric_groups_on_target(target: Target, params: CollectionParameters, groups: List[MetricGroup]) -> None: ...

def start_collection(target: Target | None = ...) -> None: ...

def pause_collection(target: Target | None = ...) -> None: ...

def resume_collection(target: Target | None = ...) -> None: ...

def stop_collection(target: Target | None = ...) -> None: ...

def save_collection(output_file_path: str) -> None: ...

def load_collection(input_file_path: str, chunk_size_bytes: int = ...) -> None: ...

def read_immediate(target: Target | None = ...) -> None: ...

def get_counter_samples(target: Target, counter: Counter) -> List[Tuple[int, Any]]: ...

def get_metric_samples(target: Target, metric: Metric) -> List[Tuple[int, Any]]: ...

class MetricStatistics:
    count: int
    min: Any
    max: Any
    avg: float | None
    def __init__(self, count: int, min: Any, max: Any, avg: float | None) -> None: ...

def get_metric_statistics_on_target(target: Target, metric: Metric) -> MetricStatistics: ...

class DiscreteHistogramBin:
    value: Any
    count: int
    def __init__(self, value: Any, count: int) -> None: ...

class MetricState:
    name: str
    description: str | None
    value: Any | None
    def __init__(self, name: str, description: str | None, value: Any | None) -> None: ...

def get_metric_discrete_histogram_on_target(target: Target, metric: Metric) -> List[DiscreteHistogramBin]: ...

def get_metric_states_on_target(target: Target, metric: Metric) -> List[MetricState]: ...

def version() -> Tuple[int, int, int, str]: ...

def status_name(code: int) -> str: ...
