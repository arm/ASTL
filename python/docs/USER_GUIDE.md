# ASTL Python User Guide

> High-level telemetry access & analytics on top of the ASTL C library.

---

## At a Glance

| Area            | Module             | Purpose                                  | Key Objects/Functions                                 |
| --------------- | ------------------ | ---------------------------------------- | ----------------------------------------------------- |
| Core Binding    | `astl._core`       | Thin Cython bridge to C API              | `get_targets`, `get_counters`,                        |
|                 |                    |                                          | `get_metrics`, `get_metric_groups`, `read_immediate`, |
|                 |                    |                                          | `get_counter_samples`, `get_metric_samples`,          |
|                 |                    |                                          | `get_metric_statistics_on_target`,                    |
|                 |                    |                                          | `get_metric_discrete_histogram_on_target`             |
| Public Facade   | `astl`             | Re-exports curated API surface           | `start_collection`, `pause_collection`,               |
|                 |                    |                                          | `resume_collection`, `stop_collection`, enums         |
| Session SerDes  | `astl`             | Save/load `.astl` sessions via C API     | `save_collection`, `load_collection`                  |
| Streaming       | `astl.streaming`   | Iterative (sync & async) polling helpers | `poll_counter_once`, `poll_metric_once`,              |
|                 |                    |                                          | `stream_counter`, `stream_metric`                     |
| Session         | `astl.session`     | Lifecycle context management             | `Session` (context manager)                           |
| Diagnostics     | `astl.diagnostics` | Environment & configuration snapshot     | `diagnostics()`                                       |
| DataFrames      | `astl.dataframe`   | Optional pandas integration              | `to_dataframe()`                                      |
| Derived Metrics | `astl.derived`     | Rate / delta computation utilities       | `deltas()`, `rates()`                                 |
| Exceptions      | `astl.exceptions`  | Semantic error mapping                   | `InitializationError`, `BadArgumentError`, ...        |

---

## Installation

Prerequisites:

- ASTL C library installed and discoverable by the loader (e.g., LD_LIBRARY_PATH/Path).
- Build toolchain per the README if compiling the C layer.

Proceed after completing the README steps, then use one of the options below.

Full instructions: see the Installation section in README.md (e.g., ../../README.md#installation).

### From Source (Editable)

```bash
python -m pip install --upgrade pip
python -m pip install -e python  # assumes repo root contains `python/`
```

### Optional Analytics Extra

```bash
python -m pip install 'pandas>=1.3'
```

If pandas is absent, `to_dataframe()` gracefully returns a list of dictionaries.

---

## Quick Start

```python
import astl
from astl.streaming import configure_basic_collection, poll_counter_once, stream_counter

# Pick a target and a counter object
t = astl.get_targets()[0]
c = astl.get_counters(t)[0]

# Minimal configuration for immediate reads
configure_basic_collection(t, counters=[c])

# One-off snapshot of a counter (returns PollResult with samples list)
snap = poll_counter_once(t, c)
print("host_ts:", snap.timestamp, "samples:", snap.samples)

# Streaming a counter (synchronous iterator)
for res in stream_counter(t, c, interval_sec=0.5, iterations=5):
    for ts, val in res.samples:
        print(ts, val)

```

---

## Architecture Overview

```text
+-----------------------------+
|        Your Application     |
+-----------------------------+
              |
              v
+-----------------------------+      High-level conveniences
|  Public Modules (astl.*)    |  <-- streaming / session / diagnostics / derived
+-----------------------------+
              |
              v
+-----------------------------+      Status -> Exception mapping
|        Cython Layer         |  <-- _core (thin wrappers)
+-----------------------------+
              |
              v
+-----------------------------+
|        ASTL C Library       |
+-----------------------------+
```

Core principle: keep binding layer minimal; push ergonomics upward.

---

## Lifecycle Management

Call order (typical):

1. (optional) configure collection parameters
2. `start_collection`
3. Read / stream / analyze
4. `pause_collection` / `resume_collection` as needed
5. `stop_collection`
6. (optional) `save_collection` to persist a `.astl` archive

Note that initialization of ASTL's internal state is done automatically

### Session Helper

```python
from astl.session import Session

t = astl.get_targets()[0]
cs = astl.get_counters(t)[:1]
with Session(target=t, counters=cs, auto_initialize=True) as sess:
    snapshot = sess.poll_once()
    print(snapshot["counters"])  # mapping: name -> list of (ts, value)
# Ensures best-effort stop on exit (with internal safety guards)
```

`Session` tolerates already-initialized state and safely no-ops on cleanup if underlying C API
reports NOT_IMPLEMENTED on certain lifecycle operations.

---

## Save / Load Session Archives

The Python facade exposes the C save/load session APIs:

- `save_collection(output_file_path: str | None = None)`
  - `None` keeps the default C behavior (save state to cache directory)
  - passing a path requests `.astl` archive output
- `load_collection(input_file_path: str, chunk_size_bytes: int = 0)`
  - loads a previously saved `.astl` archive
  - `input_file_path` must be a non-empty string
  - `chunk_size_bytes` is reserved and currently forwarded as-is

```python
import astl

t = astl.get_targets()[0]
c = astl.get_counters(t)[0]
astl.configure_basic_collection(t, counters=[c])
astl.start_collection(t)
astl.read_immediate(t)
astl.stop_collection(t)

# Persist current session to an archive
astl.save_collection("/tmp/session.astl")

# Later (or in a new process), load archived session
astl.load_collection("/tmp/session.astl")
```

Errors from the underlying C API are surfaced as `ASTLError` (or mapped subclasses).

For a runnable end-to-end example, see `python/samples/astl_demo.py`, which now
demonstrates a save/load round-trip after collection.

---

## Enumerating Telemetry

```python
import astl
t = astl.get_targets()[0]
print("Targets:", astl.get_targets())
print("Counters:", astl.get_counters(t))
print("Metrics:", astl.get_metrics(t))
print("Metric Groups:", astl.get_metric_groups(t))
```

Returned collections are typically simple Python lists / dict-like structures derived from the C API.

---

## One-Off Polling

Use an immediate read followed by sample retrieval for a given entity.

```python
from astl.streaming import poll_counter_once, poll_metric_once, configure_basic_collection

t = astl.get_targets()[0]
c = astl.get_counters(t)[0]
m = astl.get_metrics(t)[0]
configure_basic_collection(t, counters=[c], metrics=[m])

res_c = poll_counter_once(t, c)
res_m = poll_metric_once(t, m)
print(res_c.samples)
print(res_m.samples)
```

---

## Metric Summary API

After stopping collection, call `get_metric_statistics_on_target` to retrieve the minimum, maximum,
average, and sample count for any arithmetic metric without iterating raw sample lists.

```python
import astl

t = astl.get_targets()[0]
m = astl.get_metrics(t)[0]       # must be an arithmetic type (int or float)

# ... configure, start, read, stop ...

summary = astl.get_metric_statistics_on_target(t, m)
if summary.count > 0:
    print(f"count={summary.count}  min={summary.min}  max={summary.max}  avg={summary.avg:.2f}")
else:
    print("no samples collected")
```

### `MetricStatistics` Attributes

| Attribute | Type    | Notes                                                                     |
| --------- | ------- | ------------------------------------------------------------------------- |
| `min`     | `float` | Minimum sample value over all collected samples.                          |
| `max`     | `float` | Maximum sample value over all collected samples.                          |
| `avg`     | `float` | Arithmetic mean. Always a `float` (`fp64` internally).                    |
| `count`   | `int`   | Number of samples processed. When `0`, `min`/`max`/`avg` are meaningless. |

> **Important:** The ASTL C API always stores `_avg` as a `double` (`fp64`),
> regardless of whether the metric's native type is integer or float.
> The Python wrapper reads `_avg.fp64` unconditionally and returns it as a Python `float`.
> `min` and `max` are also surfaced as `float` for convenience; the underlying C union
> members match the metric's native value type.

### Metric Statistics Error Handling

| Condition                                     | Exception                                 |
| --------------------------------------------- | ----------------------------------------- |
| Non-arithmetic metric type (e.g. string/bool) | `NotSupportedError`                       |
| Invalid target or metric handle               | `BadArgumentError` / `InvalidHandleError` |

---

## Discrete Histogram API

For metrics that take a finite set of discrete values (e.g. frequency steps,
residency states), `get_metric_discrete_histogram_on_target` is more informative than a
min/max/avg summary: it returns the exact distribution of observed values.

```python
import astl

t = astl.get_targets()[0]
m = astl.get_metrics(t)[0]   # must be a supported metric type

# ... configure, start, read, stop ...

try:
    bins = astl.get_metric_discrete_histogram_on_target(t, m)
except astl.NotSupportedError:
    print("Discrete histogram not supported for this metric type")
else:
    for b in bins:
        print(f"  value={b.value}  count={b.count}")
```

### Two-step C API

Internally the function calls `astlGetMetricDiscreteHistogramBinCountOnTarget` to
allocate an exact-sized array, then `astlGetMetricDiscreteHistogramOnTarget` to fill
it. The Python wrapper hides this detail completely.

### `DiscreteHistogramBin` Attributes

| Attribute | Type  | Notes                                                            |
| --------- | ----- | ---------------------------------------------------------------- |
| `value`   | `Any` | The exact sampled value; type matches the metric's `value_type`. |
| `count`   | `int` | Number of samples whose value equals `value`. Always `>= 1`.     |

### Discrete Histogram Error Handling

| Condition                             | Exception           |
| ------------------------------------- | ------------------- |
| Unsupported metric type (e.g. `fp64`) | `NotSupportedError` |
| Invalid target or metric handle       | `BadArgumentError`  |
| Bin buffer too small (internal)       | `ASTLError`         |

---

## Streaming & Periodic Polling

### Synchronous

```python
from astl.streaming import stream_metric, configure_basic_collection

t = astl.get_targets()[0]
m = astl.get_metrics(t)[0]
configure_basic_collection(t, metrics=[m])

for res in stream_metric(t, m, interval_sec=1.0, iterations=3):
    for ts, val in res.samples:
        print(ts, val)
```

### Asynchronous

```python
import asyncio
import astl
from astl.streaming import stream_counter, configure_basic_collection

async def main():
    t = astl.get_targets()[0]
    c = astl.get_counters(t)[0]
    configure_basic_collection(t, counters=[c])
    async for res in stream_counter(t, c, interval_sec=0.25, iterations=4):
        for ts, val in res.samples:
            print(ts, val)

asyncio.run(main())
```

Streaming returns lightweight records (e.g., `PollResult`) with fields like `timestamp` and
`value`.

#### Backoff / Error Handling Strategy

- Errors raise mapped exceptions immediately (fail-fast) so calling code decides retry policy.
- If a NOT_IMPLEMENTED surface is encountered (some targets), helpers can degrade gracefully depending on the underlying C status mapping.

---

## Diagnostics Snapshot

```python
from astl import diagnostics
info = diagnostics(initialize_if_needed=True)
print(info.to_dict())
```

Use the CLI equivalent:

```bash
astl-diagnostics --json
```

(Options depend on CLI parsing implemented inside the module; run with `-h`.)

---

## Derived Metrics

`deltas()` & `rates()` operate over an iterable of `(timestamp, value)` records.

```python
from astl.derived import deltas, rates

samples = [
    (0.0, 100),
    (0.5, 150),
    (1.0, 210),
]
print(list(deltas(samples)))  # successive value differences
print(list(rates(samples)))   # value delta / time delta
```

Edge Behaviors:

- Zero or negative dt -> skipped / yields no rate.
- Non-numeric values are ignored (protecting stream continuity).

### Combining with Streaming

```python
from astl.streaming import stream_counter, configure_basic_collection
from astl.derived import rates
import astl

t = astl.get_targets()[0]
c = astl.get_counters(t)[0]
configure_basic_collection(t, counters=[c])

raw: list[tuple[int, float]] = []
for res in stream_counter(t, c, interval_sec=0.2, iterations=6):
    raw.extend(res.samples)

print(list(rates(raw)))
```

---

## DataFrame Integration (Optional)

```python
from astl.dataframe import to_dataframe
records = [
  {"timestamp": 0.0, "counter": "cpu_cycles", "value": 100},
  {"timestamp": 0.5, "counter": "cpu_cycles", "value": 150},
]
df_or_list = to_dataframe(records)  # pandas.DataFrame if pandas installed, else original list
```

If a DataFrame is produced, columns typically include: timestamp, counter / metric identifiers, numeric values.

---

## Benchmarking

Quick measurement of polling overhead:

Benchmarking helper script (benchmark_polling) has been removed; use streaming or custom timing loops instead if you need latency/throughput measurements.

---

## Exception Model

| C Status         | Python Exception          | Typical Cause                   |
| ---------------- | ------------------------- | ------------------------------- |
| BAD_ARGUMENT     | `BadArgumentError`        | Invalid parameter / ID          |
| INVALID_ARGUMENT | `InvalidArgumentError`    | Semantically invalid config     |
| NOT_IMPLEMENTED  | `NotImplementedErrorASTL` | Feature unsupported on platform |
| OUT_OF_MEMORY    | `OutOfMemoryError`        | Allocation failure              |
| INTERNAL_ERROR   | `InternalError`           | Unexpected internal failure     |
| DEPRECATED       | `DeprecatedAPIError`      | Obsolete call path              |

All raise immediately; no silent fallback except explicitly documented tolerant paths (e.g., session shutdown best-effort).

### Mapping Helper

Use `astl.map_status_to_exception(code)` when you intercept an integer status
from lower-level APIs and want the semantic subclass (returns `None` if the
status is not specially mapped yet).

This enables logging / metrics pipelines to tag failures without triggering
control-flow via exceptions.

### Reload Robustness

The exception mapping layer self-heals after module reloads
(`importlib.reload(astl.exceptions)`) and after temporary stubbing during test
setup.

Hardening tests enforce that `InitializationError` still maps correctly after
repeated reload cycles.

---

## Patterns & Best Practices

1. Stream Responsibly: Choose `interval` balancing overhead vs. granularity.
2. Derive Rates Post-Collection: Avoid computing rates inline if jitter sensitivity matters—collect raw then post-process.
3. Handle Exceptions Narrowly: Catch specific mapped exceptions instead of a broad `Exception` to retain semantic clarity.
4. Optional Dependencies: Gate analytics that rely on pandas—core telemetry should not require heavy dependencies.
5. Benchmark in CI Sparingly: Keep iterations low to reduce pipeline time; use scheduled workflows for deeper perf trending.

---

## Putting It Together (End-to-End Example)

```python
import astl
from astl.session import Session
from astl.streaming import stream_counter
from astl.derived import rates
from astl.dataframe import to_dataframe

with Session(auto_initialize=True) as sess:
    t = sess.target
    cs = sess.counters
    if t is None or not cs:
        t = astl.get_targets()[0]
        cs = astl.get_counters(t)[:1]
    raw: list[tuple[int, float]] = []
    for res in stream_counter(t, cs[0], interval_sec=0.1, iterations=10):
        raw.extend(res.samples)

rate_series = list(rates(raw))
records = [
    {"timestamp": ts, "counter": "cpu_cycles", "value": val} for ts, val in raw
]
frame_or_list = to_dataframe(records)
print("Rates:", rate_series)
print(frame_or_list)
```

---

## Troubleshooting

| Symptom                            | Likely Cause                                      | Resolution                                  |
| ---------------------------------- | ------------------------------------------------- | ------------------------------------------- |
| NotImplementedErrorASTL for metric | Platform lacks support                            | Conditional logic / skip gracefully         |
| Empty DataFrame results            | No samples collected                              | Verify streaming loop iterations / interval |
| Rates list shorter than samples    | First sample lacks predecessor / zero-dt filtered | Expected behavior                           |
| pandas ImportError                 | Extra not installed                               | `pip install pandas`                        |

---

## CLI Shortcuts

| Command            | Purpose                            |
| ------------------ | ---------------------------------- |
| `astl-diagnostics` | Print environment / target details |

---

## Extensibility Notes

- New counters/metrics auto-exposed once underlying C enumeration includes them.
- Add new derived functions beside `rates`/`deltas` (keep them pure & stream-friendly).
- For advanced export (Parquet/CSV), build atop `to_dataframe` with optional extras.

---

## Glossary

| Term     | Meaning                                            |
| -------- | -------------------------------------------------- |
| Counter  | Monotonic or raw hardware/firmware value           |
| Metric   | Possibly computed / aggregated value from counters |
| Sample   | Timestamped reading (counter or metric)            |
| Poll Gap | Time delta between consecutive poll timestamps     |

---

## License & Versioning

See `LICENSE` and `VERSION.md`. Follow semantic versioning for Python layer additions; incompatible API changes should bump the major version.

---

## Feedback

File issues or enhancement requests describing: desired metric, usage pattern, performance target, or platform constraint.

---

_End of User Guide_
