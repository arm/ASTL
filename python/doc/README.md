<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL Python Bindings

Experimental Cython-based bindings for the Arm SoC Telemetry Library
(ASTL) C API.

## Provided Functionality (Current Scope)

Core:

- `version() -> (major, minor, micro, string)`
- `Status` namespace with all status codes + `status_name(int)`
  - Numeric parity follows `include/astl/astl_errors.h` (`SUCCESS = 0` through
    `INTERNAL_ERROR = 127`).

Discovery:

- `get_targets() -> list[Target]`
- `get_counters(target) -> list[Counter]`
- `get_metrics(target) -> list[Metric]`
- `get_metric_groups() -> list[MetricGroup]`
- `get_metric_groups_on_target(target) -> list[MetricGroup]`
- `get_metric_group_metric_count(group) -> int`
- `get_metric_group_metrics(group) -> list[Metric]`
- `get_metric_group_metric_count_on_target(target, group) -> int`
- `get_metric_group_metrics_on_target(target, group) -> list[Metric]`
- `get_metric_states_on_target(target, metric) -> list[MetricState]`

  Returns the named states for `FINITE_SET_VALUE` or `RESIDENCY` metrics.
  For finite-set metrics each `MetricState` has both `name` (label) and `value`
  (decoded enumerated value). For residency metrics only `name` is populated;
  `value` is `None`. Raises `NotSupportedError` for other metric types.

Collection Configuration (per target for now):

- Collection parameters example:

  ```python
  CollectionParameters(
    sampling_interval=0,
    mode=CollectionMode.IMMEDIATE,
    flags=...
  )
  ```

- `configure_counters_on_target(target, params, counters)`
- `configure_metrics_on_target(target, params, metrics)`
- `configure_metric_groups_on_target(target, params, groups)`

Lifecycle (tolerates recoverable configuration-state statuses):

- `start_collection(target=None)` starts collection on the specified target, or
  on all configured targets when `target is None`
- `start_collection_paused(target=None)` starts collection and leaves it paused
- `pause_collection(...)` / `resume_collection(...)` /
  `stop_collection(...)`
- `read_immediate(target=None)`

Session save/load:

- `save_collection(output_file_path: Optional[str] = None)`
- `load_collection(input_file_path: str, chunk_size_bytes: int = 0)`

Post-collection processing:

- `crop_samples(start_ts=0, end_ts=0)` — permanently discard samples outside
  the given window for all targets.
- `crop_samples_on_target(target, start_ts=0, end_ts=0)`.

Samples:

- `get_counter_samples(target, counter) -> list[(timestamp, value)]`
- `get_metric_samples(target, metric) -> list[(timestamp, value)]`
- `get_metric_statistics_on_target(target, metric) -> MetricStatistics`

  Returns a `MetricStatistics` dataclass with fields `min`, `max`, `avg`, `count`
  (all Python `float`). Raises `NotSupportedError` for non-arithmetic metric types.

  > **Note:** The underlying C API always stores the average as a `double` (`fp64`)
  > regardless of the metric's value type (including integer metrics). The Python
  > wrapper reads `avg.fp64` unconditionally and exposes it as a `float`.

- `get_metric_discrete_histogram_on_target(target, metric) -> list[DiscreteHistogramBin]`

  Returns one `DiscreteHistogramBin` per unique sampled value. Each bin exposes
  `value` (native metric type) and `count` (number of samples with that exact
  value). Returns an empty list when no samples were collected. Raises
  `NotSupportedError` for metric or value types not supported by the summarizer (e.g.
  `fp64`).

Entity object attributes (read-only):

- Target: `name`, `description`, `handle_ptr`
- Counter: `name`, `description`, `min_sampling_interval`, `units`,
  `value_type`, `counter_type`, `formula`
- Metric: `name`, `description`, `min_sampling_interval`, `units`,
  `value_type`, `metric_type`, `identifier`
- MetricGroup: `name`, `description`
- MetricState: `name`, `description`, `value`

## Build / Install (Editable)

From the repository root build ASTL and install the Python package:

```sh
cmake -S . --preset debug
cmake --build --preset debug
python -m pip install -e python
```

The install step will:

1. Compile the Cython extension (`astl._core`).
2. Copy the built `libastl-*.so` into the `astl` package directory.
3. Embed an rpath (`$ORIGIN`) so the extension can load the library without
   `LD_LIBRARY_PATH`.

## Streaming & Polling Helpers

High-level helpers (in `astl.streaming`) provide synchronous and asynchronous
polling loops:

- `poll_counter_once / poll_metric_once`
- `poll_counter_periodic / poll_metric_periodic`
- `stream_counter / stream_metric` (async iterators)
- `configure_basic_collection` convenience configurator

See samples (now under the installed package tree for convenience):

- `python/samples/astl_demo.py` (core walkthrough + save/load `.astl` round-trip)
- `python/samples/astl_polling_demo.py`
- `python/samples/astl_async_demo.py`

## Session Abstraction

The `Session` context manager simplifies configure/start/stop and polling:

```python
from astl import Session, get_targets, get_counters
targets = get_targets()
if targets:
 t = targets[0]
 counters = get_counters(t)[:2]
 with Session(
  target=t,
  counters=counters,
  interval_us=1000,
  auto_initialize=False,
 ) as sess:
  snap = sess.poll_once()
  print("Snapshot counters:", {k: v[-1:] for k, v in snap['counters'].items()})
```

## Save / Load Session Archive

```python
import astl

# Save current in-memory session state to a .astl archive
astl.save_collection("/tmp/session.astl")

# Load a previously saved archive for post-processing
astl.load_collection("/tmp/session.astl")
```

## Crop Samples (Post-Collection)

After stopping collection (or after `load_collection`), permanently trim the dataset to a
time window of interest. This is useful when a loaded `.astl` archive contains more data than
needed, or to reduce memory before running summary / histogram APIs.

```python
import astl

# Crop all targets: keep only samples in [2_000_000_000, 5_000_000_000] ns
astl.crop_samples(start_ts=2_000_000_000, end_ts=5_000_000_000)

# Crop a single target
t = astl.get_targets()[0]
astl.crop_samples_on_target(t, start_ts=2_000_000_000, end_ts=5_000_000_000)
```

Key rules:

- Must be called **after** `stop_collection`. Calling while any target is STARTED or PAUSED raises
  `ASTLError` (`COLLECTION_NOT_STOPPED`).
- The operation is **irreversible** — call `load_collection` again to recover discarded samples.
- `start_ts = 0` means no lower bound; `end_ts = 0` means no upper bound.
- Timestamps use the same `CLOCK_MONOTONIC_RAW` nanosecond clock as all other ASTL APIs.

## Diagnostics

Quick environment snapshot for support / debugging:

```python
from astl import diagnostics
info = diagnostics()
print(info.to_dict())
```

Keys include `python_version`, `platform`, `astl_version`, `target_count`, and
`env_astl_config`.

Command-line shortcut after installation (editable or wheel):

```sh
astl-diagnostics
```

Produces a JSON object with the same fields.

## Enumerations

Lightweight `IntEnum` wrappers:

```python
from astl import Units, ValueType, CounterType, MetricType
print(Units.WATTS, ValueType.FLOAT64)
```

These map to the underlying C enum ordering; values may expand in future
releases.

## Exception Hierarchy

Specialized subclasses (all inherit from `ASTLError`):

- `InvalidArgumentError`
- `OutOfMemoryError`
- `InternalError`

The internal mapping layer can be expanded; for now they are available for
consumer code that wishes to raise or catch them explicitly.

## DataFrame Helper

If `pandas` is installed, you can convert a mapping of name -> samples into a
DataFrame:

```python
from astl.dataframe import to_dataframe
snap = {"counter0": [(1, 10), (2, 15)], "counter1": [(1, 5), (2, 9)]}
df = to_dataframe(snap, silent=True)
print(df.head())
```

If pandas is absent and `silent=False` (default) an ImportError is
raised.

## Derived Metrics

Simple utilities for post-processing sample lists:

```python
from astl.derived import deltas, rates
samples = [(1000, 10), (2000, 25), (3000, 40)]  # (timestamp, value)
print(deltas(samples))  # successive differences
print(rates(samples, time_scale=1000))  # per 'second' if timestamps were ms
```

See `python/samples/astl_demo.py` for a minimal end-to-end flow, including
`save_collection(...)` and `load_collection(...)` usage.

```python
import astl
print("Version:", astl.version())

targets = astl.get_targets()
if not targets:
 print("No targets detected")
else:
 t = targets[0]
 counters = astl.get_counters(t)
 metrics = astl.get_metrics(t)
 print(
  f"First target {t.name} has {len(counters)} counters, {len(metrics)} metrics"
 )

 # Configure an immediate collection for first two counters (if available)
 if counters:
  params = astl.CollectionParameters(
   sampling_interval=100,
   mode=astl.CollectionMode.IMMEDIATE,
  )
  astl.configure_counters_on_target(t, params, counters[:2])
  astl.start_collection_paused(t)
  astl.resume_collection(t)
  # In a real app you'd wait / sleep, then stop
  astl.stop_collection(t)
  for c in counters[:2]:
   samples = astl.get_counter_samples(t, c)
   print(c.name, samples[:3])
```

## Notes

These bindings assume the shared library name pattern produced by CMake
(`libastl-<MAJOR>.so`). You should no longer need to export `LD_LIBRARY_PATH`
because the setup step copies the shared library beside the extension and sets
an rpath. If import fails
unexpectedly, verify the native library was built (look for
`build/debug/lib/libastl-*.so`) and reinstall the editable package.
