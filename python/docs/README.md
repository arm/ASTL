# ASTL Python Bindings

Experimental Cython-based bindings for the Arm SoC Telemetry Library
(ASTL) C API.

## Provided Functionality (Current Scope)

Core:

- `initialize(config_path: Optional[str]) -> None`
- `version() -> (major, minor, micro, string)`
- `Status` namespace with all status codes + `status_name(int)`

Discovery:

- `get_targets() -> list[Target]`
- `get_counters(target) -> list[Counter]`
- `get_metrics(target) -> list[Metric]`
- `get_metric_groups(target) -> list[MetricGroup]`

Collection Configuration (per target for now):

- Collection parameters example:

  ```python
  CollectionParameters(
    sampling_interval=0,
    mode=CollectionMode.IMMEDIATE,
    optimization=...
  )
  ```

- `configure_counters_on_target(target, params, counters)`
- `configure_metrics_on_target(target, params, metrics)`
- `configure_metric_groups_on_target(target, params, groups)`

Lifecycle (tolerates NOT_IMPLEMENTED by ignoring it):

- `start_collection(target=None)` /
  `pause_collection(...)` / `resume_collection(...)` /
  `stop_collection(...)`
- `read_immediate(target=None)`

Session save/load:

- `save_collection(output_file_path: Optional[str] = None)`
- `load_collection(input_file_path: str, chunk_size_bytes: int = 0)`

Samples:

- `get_counter_samples(target, counter) -> list[(timestamp, value)]`
- `get_metric_samples(target, metric) -> list[(timestamp, value)]`

Entity object attributes (read-only):

- Target: `name`, `description`, `_handle_ptr`
- Counter: `name`, `description`, `min_sampling_interval`, `units`,
  `value_type`,
  `counter_type`, `mask`, `formula`
- Metric: `name`, `description`, `min_sampling_interval`, `units`,
  `value_type`, `metric_type`
- MetricGroup: `name`, `description`, `metric_count`

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
from astl import Session, get_targets, get_counters, initialize

initialize(None)
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

- `InitializationError`
- `NotImplementedErrorASTL`
- `InvalidArgumentError`
- `OutOfMemoryError`

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

astl.initialize(None)
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
  astl.start_collection(t)
  # In a real app you'd wait / sleep, then stop
  astl.stop_collection(t)
  for c in counters[:2]:
   samples = astl.get_counter_samples(t, c)
   print(c.name, samples[:3])
```

If you forget to call `initialize()` first, API calls will raise:

```text
ASTLError: NOT_INITIALIZED (call astl.initialize() before using this function)
```

## Notes

These bindings assume the shared library name pattern produced by CMake
(`libastl-<MAJOR>.so`). You should no longer need to export `LD_LIBRARY_PATH`
because the setup step copies the shared library beside the extension and sets
an rpath. If import fails
unexpectedly, verify the native library was built (look for
`build/debug/lib/libastl-*.so`) and reinstall the editable package.
