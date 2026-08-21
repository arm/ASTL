<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL

[![Integration](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml/badge.svg)](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml)
[![Maintainability](https://qlty.sh/badges/6f288530-d295-4eb4-a8d3-9fab05020fcb/maintainability.svg)](https://qlty.sh/gh/Arm-Debug/projects/ASTL)
[![Code Coverage](https://qlty.sh/badges/6f288530-d295-4eb4-a8d3-9fab05020fcb/coverage.svg)](https://qlty.sh/gh/Arm-Debug/projects/ASTL)
<a href="https://arm.app.blackduck.com/api/projects/bb3f58ac-a952-4b1c-8561-61a04d23bf57">
<img src="https://github.com/Arm-Debug/ASTL/actions/workflows/blackduck.yaml/badge.svg" alt="Blackduck"/>
</a>

Arm SoC Telemetry Library

## Description

ASTL is a library for SoC telemetry collection at Arm. It abstracts low level
interfaces to telemetry data sources on the system. Using a predefined API, a telemetry
collection tool or an AI framework can dynamically discover available supported telemetry on
the target platform, configure, start, (pause, resume), stop a collection and process
collected data. Collected data can be streamed directly to a user provided buffer or can be
written to a specified output file format, such as a Perfetto JSON file for data visualization.

See the dedicated "Output Formats" section near the end of this document for full details on the
currently supported output mechanisms (in-memory buffer, Perfetto trace, Interval CSV, and Summary CSV).

The initial implementation focuses on the System Control and Management Interface (SCMI)
specification through the Linux SCMI telemetry ioctl and legacy sysfs interfaces. It also supports
hwmon telemetry through libsensors. It may eventually be expanded to add support for other
interfaces such as: BIOS mailboxes, PCIe configuration spaces, direct register accesses,
PROCFS, OS provided data or other sources of data.

The library has a C-interface for the API and a C++ implementation. A comprehensive experimental
Python wrapper layer (Cython bindings + high-level utilities) is now available—refer to the
**[Python User Guide](python/doc/USER_GUIDE.md)**.

A native Go wrapper is also available under
**[Go/](Go/README.md)** for Go programs that want to call the ASTL C API
through cgo.

## Key Goals and Properties

### Sharable

- New and existing tools at Arm can use it.
- Partners and external third-party tool developers can use it to access telemetry on Arm platforms.

### Uniform

- Telemetry collection through a fixed, predefined API surface.

### Portable

- Rebuild on Windows or other OSs with the same user API interface.
- Python wrapper layer support.

### Extensible

- Driver-to-driver context-switch based collection.
- SCMI specification extensions.
- Additional platform-level telemetry access mechanisms.

### Reusable

- Can be deployed across IoT, automotive, client, data center, GPU, and NPU platforms.
- Can be used by telemetry collection tools, AI frameworks, or directly to instrument workloads.

## High Level Architecture Diagram

![image](https://github.com/user-attachments/assets/0cc5580e-eb22-4219-9118-adb486972032)

## Telemetry Collection Tool Usage Example Diagram

![image](https://github.com/user-attachments/assets/ee543a10-fae6-45a8-8305-7cce78a3521b)

## AI Framework Usage Example Diagram

![image](https://github.com/user-attachments/assets/e514cfb8-7d15-45f6-899e-2b70c2c6c5db)

## High level Internal Design and status

<img width="708" height="436" alt="image" src="https://github.com/user-attachments/assets/16aceb9e-b837-47fc-a2d9-e7fee2a3d236" />

## Testing and Isolation Methodology

![image](https://github.com/user-attachments/assets/0a2b1e39-cb08-4e04-9f62-bba5329bfe56)

- Complete isolation from all dependencies
- Mock mode SCMI sysfs file/folder generator
- Mock mode data generator

- Bench testing suite
- Trompleil and Catch2 unit testing
- Mock command line collector executable

## Installation and usage

### Installation

ASTL's `config` directory holds platform-specific metrics specifications
and should be included in distributions of the binary library.
ASTL looks for it in the following directories in preferred order:

1. Environment variable override: `ASTL_CONFIG_DIR`
2. Under a user-specific application data dir, depending on OS

- Linux : `$XDG_DATA_HOME/astl/config` -> defaults to `~/.local/share/astl/config`
- Mac : `~/Library/Application Support/astl/config`
- Windows: `%LOCALAPPDATA%\astl\config`

3. System-wide application resource directory, depending on OS

- Linux : `/usr/local/share/astl/config`
- Mac : `/Library/Application Support/astl/config`
- Windows: `%PROGRAMDATA%\astl\config`

4. Default to fallback of relative location by path to astl library.
   For instance, if the library is at `~/Downloads/astl/libastl.so`, ASTL looks in `~/Downloads/astl/config/`

See [Build steps for developers](#build-steps-for-developers) for more detailed build instructions.

> Looking for the Python telemetry wrapper? See the
> **[ASTL Python User Guide](python/doc/USER_GUIDE.md)** for: initialization, streaming (sync &
> async), diagnostics CLI, derived metrics, DataFrame integration, benchmarking,
> metric state discovery (`get_metric_states_on_target` / `MetricState`), and
> exception model.

### Usage

The complete flow is demonstrated in [`samples/sample_test.cpp`](samples/sample_test.cpp). Below are the minimal snippets you need:

```cpp
#include "astl/astl.h"          // core API

#include "astl/astl_telemetry.h"     // Function calls
```

`astl_telemetry.h` is self-contained for pure C consumers as well. The helper macros
`ASTL_INIT_STRUCT`, `ASTL_ALLOC_ARRAY`, and `ASTL_FREE_ARRAY` are intended to work in both C and C++ translation units.

1. Provide an SCMI telemetry interface.

By default, ASTL automatically probes SCMI telemetry ioctl character devices under
`/dev/scmi` and uses them when available. If no usable ioctl target is found,
ASTL falls back to the legacy SCMI telemetry sysfs interface under
`/sys/fs/arm_telemetry`.

For legacy sysfs-only systems, mount the sysfs interface:

```bash
mount -t stlmfs none /sys/fs/arm_telemetry/
```

2. Initialize ASTL

If needed, use these environment variables to choose or redirect the SCMI backend:

- `ASTL_COLLECTORS`: comma-separated allowlist of collectors used for live
  discovery. Supported names are `scmi`, `libsensors`, and `procfs`. Unset or
  empty enables every collector included in the build. Unknown or unavailable
  names cause initialization to fail.
- `ASTL_SCMI_INTERFACE`: SCMI backend preference. Accepted values are `auto`,
  `ioctl`, and `sysfs`; unset or unknown values use `auto`.
- `ASTL_SCMI_IOCTL_DEV_ROOT`: ioctl device root. Defaults to `/dev/scmi`.
- `ASTL_SCMI_SYSFS_TELEMETRY_ROOT`: legacy sysfs telemetry root. Defaults to
  `/sys/fs/arm_telemetry`.

Some developers might have a reason to use modified platform definition and metrics config files.
You can use ASTL_CONFIG_DIR for this.

```bash
# optional - discover only SCMI targets and metrics
export ASTL_COLLECTORS="scmi"
# optional - force the legacy sysfs backend, for example when using MockSysfs
export ASTL_SCMI_INTERFACE="sysfs"
# optional - if SCMI ioctl devices are not under /dev/scmi
export ASTL_SCMI_IOCTL_DEV_ROOT="/path/to/scmi-devices"
# optional - if your SCMI sysfs is not in the expected mount point
export ASTL_SCMI_SYSFS_TELEMETRY_ROOT="/sys/fs/arm_telemetry"
# optional - if you're hacking around with metric definitions
export ASTL_CONFIG_DIR="/path/to/my_astl/config"
```

For `libsensors` targets, ASTL also looks for optional metric metadata files under
`$ASTL_CONFIG_DIR/metrics/libsensors/` or the built-in `config/metrics/libsensors/` directory.
ASTL first looks for an exact file matching the discovered target name and then falls back to progressively less
specific chip-family files. For example, an `nvme-pci-40100` target will fall back to `libsensors_nvme-pci.json` if no
exact override exists. This avoids relying on PCI addresses to predict which subfeatures a device exposes.
If no file exists, ASTL discovers and registers all readable sensor metrics across all supported feature families.
If an exact target file exists, it acts as an allowlist: only declared metrics are registered, and ASTL warns when a
declared metric is not present on the current system. If a fallback family file exists, ASTL still uses lm-sensors
enumeration to decide which metrics exist on the current machine and only uses the JSON file as a metadata overlay for
matching sensors. Undeclared discovered sensors continue to register with default metadata in that fallback case.
These declaration files can attach descriptions, metric groups, and formulas. Any explicit scaling should now be
expressed directly in `formula`, for example `value / 1000`. To reduce duplication, a libsensors declaration file may
also use a relative `"extends"` path to inherit from a shared template file. Child files override any top-level
metadata and replace or add individual metrics by key.

For repetitive derived metric declarations, libsensors metrics also support an optional
`"derived_metrics"` object on a base metric. This is used to automatically generate related metrics from a single source
(e.g., alarm/limit variants of a temperature or voltage sensor). Each key selects the derived metric kind. Supported keys include:
`low`/`min`, `high`/`max`, `critical`/`crit`, `emergency`/`emerg`, `alarm`, and `beep`. Each nested object can override
fields such as `description`. ASTL automatically expands those into sibling declaration entries (e.g.,
`Composite_thermal_limit_high`, `Composite_thermal_limit_emergency`, `Composite_alarm`), preserving the base metric's
units, formula, and metric groups. When metrics are discovered via lm-sensors or a fallback family file,
unsupported or unavailable derived metric keys are silently skipped if the corresponding subfeature is not present
on the current system. When an exact target file is used as an allowlist, ASTL instead emits a warning for each
declared derived metric whose subfeature is not observed on the current system.

Within the ASTL_CONFIG_DIR path, you can override definitions of metrics, which look like:

```json
    "Throttle Counts": {
      "description": "Number of thermal throttling events",
      "unit": "",
      "metric_type": "delta",
      "identifier": "THERMAL_THROTTLE",
      "metric_groups": ["throttling"],
      "collection": {
        "register": "THROTTLE_EVENTS",
        "protocol": "scmi"
      }
    }

```

Each metric: has a name as a key, along with the following fields:

register: the exact name of the register where ASTL should read this metric's data (e.g. a `layout/member` key in the SCMI spec)
unit: selects which `astl_units_t` the metric is associated with
metric_type: selects the `astl_metric_type_t` for this data
collection_protocol: selects which collectors should measure it

For more details on metrics declarations and platform-specific scmi spec, see [doc/config_and_specification_files.md](doc/config_and_specification_files.md)

### Formula Support

ASTL supports flexible data transformation through formulas that can be applied to raw metric values. Formulas allow you to:

- Extract bit fields from raw register values
- Apply integer scaling and transformations
- Perform complex mathematical operations
- Combine bitwise and arithmetic operations

#### Formula Types

**String Expressions** (using [tinyexpr++](https://github.com/Blake-Madden/tinyexpr-plusplus) in uint64_t mode):
Express transformations as mathematical expressions using the variable name `value`:

```json
{
  "metrics": {
    "scaled_value": {
      "register": "sensor_raw",
      "unit": "none",
      "formula": "value / 1000"
    },
    "power_watts": {
      "register": "power_sensor",
      "unit": "watts",
      "formula": "value / 1000"
    },
    "combined_bits": {
      "register": "control_reg",
      "unit": "none",
      "formula": "(value & 0xFF00) | 0x42"
    }
  }
}
```

**Supported Operations:**

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Bitwise: `&` (AND), `|` (OR), `^` (XOR), `~` (NOT)
- Shift: `>>` (right shift), `<<` (left shift)
- Logical: `&&`, `||`, `!`
- Comparison: `<`, `>`, `<=`, `>=`, `==`, `!=`
- Parentheses for grouping

**UINT64_T Mode:**
TinyExpr++ is compiled with `TE_UINT64` and `TE_BITWISE_OPERATORS`, providing exact uint64_t arithmetic without floating-point conversion. All operations preserve full 64-bit precision.

**Important Notes:**

- **Floating-point literals** (e.g., `0.001`, `0.5`) will be truncated to integers, causing precision loss
- ❌ **Incorrect**: `"value * 0.001"` → truncates `0.001` to `0`, resulting in `value * 0 = 0`
- ✅ **Correct**: `"value / 1000"` → proper integer division
- For more details on expression syntax, see the [tinyexpr++ documentation](https://github.com/Blake-Madden/tinyexpr-plusplus#functions)

---

3. Discover targets

```cpp
ASTL_INIT_STRUCT(astl_get_target_count_params_t, get_target_count_params,
                 .flags = 0,
                 .target_count = &target_count);
status = astlGetTargetCount(&get_target_count_params);
if (status != ASTL_STATUS_SUCCESS || target_count == 0) {
  // skip astlGetTargets when target_count == 0
  return;
}
// allocate an array to hold the properties of each target
ASTL_ALLOC_ARRAY(astl_target_props_t, target_properties_buffer, target_count);
ASTL_INIT_STRUCT(astl_get_targets_params_t, get_targets_params,
                 .flags = 0,
                 .targets = target_properties_buffer,
                 .target_count = &target_count);
status = astlGetTargets(&get_targets_params);
...
// using the first target
astl_target_props_t target_properties = target_properties_buffer[0];
```

4. Configure collection

Calling any `astlConfigure*Collection*` API starts a clean collection session when collection is not
running. If a previous collection has reached `STOPPED`, retrieve or save any samples you still need
before configuring again. The next configure call clears previous collection-scoped data such as
cached samples, processed samples, clock correlations, and operation mappings. Target discovery,
metric definitions, and metric handles remain available.

```cpp
uint32_t metric_count{};
ASTL_INIT_STRUCT(astl_get_metric_count_params_t, get_metric_count_params,
    .flags = 0,
    .target_handle = target_properties.handle,
    .metric_count = &metric_count);
astlGetMetricCountOnTarget(&get_metric_count_params);
if (metric_count == 0) {
    // skip astlGetMetricsOnTarget when metric_count == 0
    return;
}
std::vector<astl_metric_props_t> metric_buffer(metric_count);
metric_buffer[0].size = sizeof(astl_metric_props_t);
ASTL_INIT_STRUCT(astl_get_metrics_params_t, get_metrics_params,
    .flags = 0,
    .target_handle = target_properties.handle,
    .metrics = metric_buffer.data(),
    .metric_count = &metric_count);
status = astlGetMetricsOnTarget(&get_metrics_params);

ASTL_INIT_STRUCT(astl_collection_params_t, collection_params,
    .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD,
    .sampling_interval = 0,
    .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE,
);
ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, configure_metric_params,
    .flags = 0,
    .target_handle = target_properties.handle,
    .collection_params = &collection_params,
    .metric_handles = &metric_buffer.front().handle,
    .metric_count = metric_count);
status = astlConfigureMetricCollectionOnTarget(&configure_metric_params);
```

After a target is configured, ASTL can also create a native event metric for that target named
`astl_lifecycle_events.<target-name>`. This metric is generated by ASTL itself (not by an external
hardware collector) and records lifecycle events (pause, resume, and crop boundaries) for that
target. It is created lazily, the first time one of those lifecycle events actually occurs for the
target (i.e. on the first pause, resume, or crop), so it does not exist for sessions that never
pause, resume, or crop.

Because this metric is created only once a lifecycle event has happened, you must rediscover the
metrics for that target after the first pause/resume/crop if you need the synthetic
`astl_lifecycle_events.<target-name>` metric in the collection and want to take actions on it later,
such as filtering or cropping. It will not appear in a metric list retrieved before the first
lifecycle event. For a given target, call `astlGetMetricCountOnTarget(...)` again, then call
`astlGetMetricsOnTarget(...)` again to fetch the updated metric set and locate the new metric handle.

Alternatively, we can configure collection by metric groups

```cpp
auto     CollectFirstGroup(astl_target_handle_t target) -> void {
uint32_t metric_group_count{};
ASTL_INIT_STRUCT(astl_get_metric_group_count_on_target_params_t, get_group_count_params,
                 .flags = 0,
                 .target_handle = target,
                 .metric_group_count = &metric_group_count);
auto status = astlGetMetricGroupCountOnTarget(&get_group_count_params);

std::vector<astl_metric_group_props_t> metric_groups_properties(metric_group_count);
metric_groups_properties[0].size = sizeof(astl_metric_group_props_t);

// retrieve the metric groups
ASTL_INIT_STRUCT(astl_get_metric_groups_on_target_params_t, get_groups_params,
                 .flags = 0,
                 .target_handle = target,
                 .metric_groups = metric_groups_properties.data(),
                 .metric_group_count = &metric_group_count);
status = astlGetMetricGroupsOnTarget(&get_groups_params);

// collect on the first group (you could instead look at the properties and filter by name)
std::vector<astl_metric_group_handle_t> groups{metric_groups_properties[0].handle};
const uint32_t groups_count = static_cast<uint32_t>(groups.size());
ASTL_INIT_STRUCT(astl_collection_params_t, collection_params,
                 .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD,
                 .sampling_interval = 100,
                 .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE);

ASTL_INIT_STRUCT(astl_configure_metric_group_collection_on_target_params_t, configure_group_params,
                 .flags = 0,
                 .target_handle = target,
                 .collection_params = &collection_params,
                 .metric_group_handles = groups.data(),
                 .metric_group_count = groups_count);
status = astlConfigureMetricGroupCollectionOnTarget(&configure_group_params);
}
```

The same rediscovery rule applies when you configure by metric group: once a lifecycle event
(pause, resume, or crop) first occurs for a target, call `astlGetMetricCountOnTarget(...)` and
`astlGetMetricsOnTarget(...)` again for that target if you also need the synthetic
`astl_lifecycle_events.<target-name>` metric in the collection and want to filter or crop it later.

If you need the metrics that belong to a metric group regardless of target, first retrieve the
global group descriptors with `astlGetMetricGroups(...)`, then call
`astlGetMetricGroupMetricCount(...)` to size the buffer and `astlGetMetricGroupMetrics(...)` to
fetch the metric properties. Use `astlGetMetricGroupMetricCountOnTarget(...)` plus
`astlGetMetricGroupMetricsOnTarget(...)` when you need the group membership filtered to a specific
target.

5. Start, read, and stop collection (including paused start)

```cpp
ASTL_INIT_STRUCT(astl_start_collection_on_target_params_t, start_params,
                 .flags = 0,
                 .target_handle = target_properties.handle);
status = astlStartCollectionOnTarget(&start_params);

ASTL_INIT_STRUCT(astl_start_collection_on_target_paused_params_t, start_paused_params,
                 .flags = 0,
                 .target_handle = target_properties.handle);
status = astlStartCollectionOnTargetPaused(&start_paused_params);  // final state after call: PAUSED

ASTL_INIT_STRUCT(astl_start_collection_paused_params_t, start_all_paused_params,
                 .flags = 0);
status = astlStartCollectionPaused(&start_all_paused_params);  // all CONFIGURED targets -> PAUSED

ASTL_INIT_STRUCT(astl_start_collection_params_t, start_all_params,
                 .flags = 0);
status = astlStartCollection(&start_all_params);  // all CONFIGURED targets -> STARTED

// Aggregate starts are transactional: if one CONFIGURED target fails to start,
// ASTL stops any earlier targets started by the same call before returning.

// Optional: temporarily suspend sampling after a normal start
ASTL_INIT_STRUCT(astl_pause_collection_on_target_params_t, pause_params,
                 .flags = 0,
                 .target_handle = target_properties.handle);
status = astlPauseCollectionOnTarget(&pause_params);   // collection state: PAUSED
ASTL_INIT_STRUCT(astl_resume_collection_on_target_params_t, resume_params,
                 .flags = 0,
                 .target_handle = target_properties.handle);
status = astlResumeCollectionOnTarget(&resume_params);  // back to STARTED

ASTL_INIT_STRUCT(astl_read_immediate_on_target_params_t, immediate_params,
                 .flags = 0,
                 .target_handle = target_properties.handle);
status = astlReadImmediateOnTarget(&immediate_params);     // useful while CONFIGURED, STARTED, or PAUSED
ASTL_INIT_STRUCT(astl_stop_collection_on_target_params_t, stop_params,
                 .flags = 0,
                 .target_handle = target_properties.handle);
status = astlStopCollectionOnTarget(&stop_params);    // final state: STOPPED
```

6. Save or load a session (.astl)

ASTL can serialize a completed collection session into a single `.astl` file (and later reload it) for
post-processing/output generation.

Key rules:

- Always set the struct `size` field to `sizeof(struct)`.
- `flags` must be `0` (reserved for future use).
- Call `astlSaveCollection()` after stopping collection.
- Save before calling any configure API for a new collection if you need to keep the stopped
  session's samples.
- After `astlLoadCollection()`, collection control APIs are disabled; only post-processing/output generation is possible.

#### Save

```c
#include "astl/astl_telemetry.h"

ASTL_INIT_STRUCT(astl_save_params_t, params,
                 .output_file_path = "/tmp/session.astl",  // nullptr/empty => temp files only
                 .flags = 0);

astl_status_code rc = astlSaveCollection(&params);
```

#### Load / import

```c
#include "astl/astl_telemetry.h"

ASTL_INIT_STRUCT(astl_load_params_t, params,
                 .input_file_path = "/tmp/session.astl",
                 .chunk_size_bytes = 0,  // reserved; 0 uses default
                 .flags = 0);

astl_status_code rc = astlLoadCollection(&params);
```

7. Crop samples (optional, post-collection)

`astlCropSamples` and `astlCropSamplesOnTarget` permanently discard any samples that fall outside
a supplied time window. This is useful when a loaded `.astl` archive contains more data than needed
for analysis, or to trim a live session before saving.

Key rules:

> [!CAUTION]
> The crop operation is **irreversible**. Once samples are discarded they cannot be recovered.

- Both `astlCropSamples` and `astlCropSamplesOnTarget` currently return `ASTL_STATUS_NOT_IMPLEMENTED`; the API surface is declared and the structs are stable, but the implementation is pending.
- Collection must be **stopped** before cropping; calling while a target is STARTED or PAUSED will return `ASTL_STATUS_COLLECTION_NOT_STOPPED` once implemented.
- A window with `start_ts = 0` has no lower bound; `end_ts = 0` has no upper bound.
- `window_count` must be ≥ 1.

#### Crop all targets

```c
#include "astl/astl_telemetry.h"

astl_crop_window_t window = {0};
window.size     = sizeof(astl_crop_window_t);
window.start_ts = 1000000000ULL;  // retain samples from 1 s onwards
window.end_ts   = 5000000000ULL;  // up to 5 s

astl_crop_samples_params_t params = {0};
params.size         = sizeof(astl_crop_samples_params_t);
params.windows      = &window;
params.window_count = 1;

astl_status_code rc = astlCropSamples(&params);  // returns NOT_IMPLEMENTED for now
```

#### Crop a specific target

```c
#include "astl/astl_telemetry.h"

astl_crop_window_t window = {0};
window.size     = sizeof(astl_crop_window_t);
window.start_ts = 1000000000ULL;
window.end_ts   = 5000000000ULL;

astl_crop_samples_on_target_params_t params = {0};
params.size          = sizeof(astl_crop_samples_on_target_params_t);
params.target_handle = target_props.handle;
params.windows       = &window;
params.window_count  = 1;

astl_status_code rc = astlCropSamplesOnTarget(&params);  // returns NOT_IMPLEMENTED for now
```

8. Retrieve metric samples. Logs are written to:

- `raw_samples.log` and `sampled_value_summary.log` - metric data (raw + summarized)
- `sysfs.log` - mock SCMI driver output

9. Compute min/max/avg summary for a metric (post-collection)

After stopping collection, call `astlGetMetricStatisticsOnTarget` for any arithmetic metric to obtain
the minimum, maximum, average, and sample count over all collected samples. See the
[Metric Summary API](#metric-summary-api) section for full details.

```c
#include "astl/astl_telemetry.h"
#include <inttypes.h>
#include <stdio.h>

// Retrieve metric statistics (min/max/avg)
astl_target_props_t* target_properties = /* initialized from astlGetTargets */;
astl_metric_props_t* metric_buffer = /* initialized from astlGetMetricsOnTarget */;

astl_metric_statistics_t summary = {0}; // zero-initialize
summary.size = sizeof(astl_metric_statistics_t);
summary.flags = ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG;

ASTL_INIT_STRUCT(astl_get_metric_statistics_on_target_params_t, get_metric_stats_params,
.flags = 0,
.target_handle = target_properties.handle,
.metric_handle = metric_buffer[0].handle,
.summary = &summary);
astl_status_code rc = astlGetMetricStatisticsOnTarget(&get_metric_stats_params);

if (rc == ASTL_STATUS_SUCCESS && summary.count > 0) {
// avg is always fp64 — safe for all arithmetic metric types
printf("count=%" PRIu64 " avg=%.2f\n",
summary.count, summary.avg.fp64);
}

```

10. Retrieve histogram bins for finite-set metrics (post-collection)

For metrics that take a finite set of values (e.g. frequency steps, residency states),
use the two-step `astlGetMetricDiscreteHistogramBinCountOnTarget` /
`astlGetMetricDiscreteHistogramOnTarget` API to obtain the exact distribution of observed values.
See the [Discrete Histogram API](#discrete-histogram-api) section for full details.

```cpp
uint32_t bin_count = 0;
ASTL_INIT_STRUCT(astl_get_metric_discrete_histogram_bin_count_on_target_params_t,
                 bin_count_params,
                 .flags = 0,
                 .target_handle = target_properties.handle,
                 .metric_handle = metric_buffer[0].handle,
                 .bin_count = &bin_count);
astl_status_code rc = astlGetMetricDiscreteHistogramBinCountOnTarget(&bin_count_params);

if (rc == ASTL_STATUS_SUCCESS && bin_count > 0) {
    std::vector<astl_discrete_histogram_bin_t> bins(bin_count);
    bins[0].size = sizeof(astl_discrete_histogram_bin_t);

    ASTL_INIT_STRUCT(astl_get_metric_discrete_histogram_on_target_params_t,
                     histogram_params,
                     .flags = 0,
                     .target_handle = target_properties.handle,
                     .metric_handle = metric_buffer[0].handle,
                     .bins = bins.data(),
                     .bin_count = &bin_count);
    rc = astlGetMetricDiscreteHistogramOnTarget(&histogram_params);

    if (rc == ASTL_STATUS_SUCCESS) {
        for (uint32_t i = 0; i < bin_count; ++i)
            printf("value=%.0f  count=%u\n",
                   (double)bins[i].value.ui64, bins[i].count);
    }
}
```

11. Clean up allocated resources

```cpp
ASTL_FREE_ARRAY(target_properties_buffer)
```

Run `scripts/demo.sh` to run this flow. This sets up the mock driver and performs a sample run.
To run manually, start a mock server and execute `build/debug/bin/sample_test`. Use `--help`
for usage details.

## Metric Summary API

`astlGetMetricStatisticsOnTarget` computes min/max/avg/count over all collected samples for a metric on a target.
Call it after `astlStopCollectionOnTarget` or after `astlLoadCollection`.

`astl_metric_statistics_t` fields:

- `size`: Must be `sizeof(astl_metric_statistics_t)`.
- `flags`: `ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG` or `ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG`.
- `min` and `max`: Union members matching the metric's native value type.
- `avg`: Always read `summary.avg.fp64`.
- `count`: Number of samples included in the summary.

Common status codes:

- `ASTL_STATUS_SUCCESS`
- `ASTL_STATUS_BAD_ARGUMENT`
- `ASTL_STATUS_INVALID_FLAG_VALUE`
- `ASTL_STATUS_OLD_STRUCT_VERSION`
- `ASTL_STATUS_NEW_STRUCT_VERSION`
- `ASTL_STATUS_NOT_SUPPORTED`

`astl_status_code` numbering is contiguous for public statuses (`0..43`), with
`ASTL_STATUS_UNKNOWN_ERROR = -1` and `ASTL_STATUS_INTERNAL_ERROR = 127` reserved.

## String Pointer Lifetimes and Ownership

**Important:** The `astlGetMetricStatesOnTarget` API returns `const char*` pointers to state name strings (`astl_state_props_t.name`). These pointers refer to internal storage owned by ASTL's metric and configuration objects.

### Lifetime Guarantees

State name pointers are valid only for the current collection session:

1. **During a collection session**: All state name pointers returned by `astlGetMetricStatesOnTarget` remain valid throughout the current collection session (across start/stop/pause/resume cycles).

2. **After reconfiguration**: State name pointers become **invalid** immediately when ASTL is reconfigured for a subsequent collection. Accessing these pointers after reconfiguration results in undefined behavior.

### Usage Recommendations

- **Within a session**: If you only need state name values during a single collection session, use the pointers directly without copying.

- **Across sessions**: If you need to retain state name values across multiple collection sessions or after reconfiguration, **copy the strings** into your own storage:

  ```cpp
  // Safe: Copy the string for long-term storage across sessions
  std::string state_name = states[i].name;

  // Safe: Use within the same collection session
  printf("State: %s\n", states[i].name);

  // UNSAFE: Storing pointer for use after reconfiguration
  const char* saved_ptr = states[i].name;  // ❌ Dangling pointer after reconfiguration
  // ... reconfigure ASTL for next collection ...
  printf("State: %s\n", saved_ptr);  // ❌ Undefined behavior
  ```

## Design Diagrams (Mermaid)

The detailed runtime and structural diagrams are authored in Mermaid (`doc/design/*.mmd`) and rendered to SVG with:

```sh
node scripts/render_mermaid.js --all
```

1. `system_phase_init_discovery.mmd` – Initialization & target/metric discovery
2. `system_phase_metric_config.mmd` – Metric configuration & operations derivation
3. `system_phase_collection.mmd` – Interval sampling loop, immediate reads, pause/resume stubs
4. `system_phase_stop_processing.mmd` – Deferred processing at stop & summarization
5. `system_phase_retrieval_shutdown.mmd` – Retrieval APIs, shutdown, representative errors

`system_end_to_end_sequence.mmd` remains as an overview referencing those phases.

## Discrete Histogram API

`astlGetMetricDiscreteHistogramBinCountOnTarget` and `astlGetMetricDiscreteHistogramOnTarget` implement a
two-step API that returns the exact distribution of observed values for a metric — one bin
per unique value. Both functions are called after `astlStopCollectionOnTarget` (or after `astlLoadCollection`).

### Two-step Usage Pattern

```c
#include "astl/astl_telemetry.h"
#include <stdlib.h>
#include <stdio.h>

/* Step 1 – query the number of unique-value bins */
uint32_t bin_count = 0;
ASTL_INIT_STRUCT(astl_get_metric_discrete_histogram_bin_count_on_target_params_t,
                 bin_count_params,
                 .flags = 0,
                 .target_handle = target_handle,
                 .metric_handle = metric_handle,
                 .bin_count = &bin_count);
astl_status_code rc = astlGetMetricDiscreteHistogramBinCountOnTarget(&bin_count_params);

if (rc == ASTL_STATUS_SUCCESS && bin_count > 0) {
    /* Step 2 – allocate and fill */
    astl_discrete_histogram_bin_t* bins =
        (astl_discrete_histogram_bin_t*)calloc(bin_count, sizeof(*bins));
    if (bins) {
        bins[0].size = sizeof(astl_discrete_histogram_bin_t);

        ASTL_INIT_STRUCT(astl_get_metric_discrete_histogram_on_target_params_t,
                         histogram_params,
                         .flags = 0,
                         .target_handle = target_handle,
                         .metric_handle = metric_handle,
                         .bins = bins,
                         .bin_count = &bin_count);
        rc = astlGetMetricDiscreteHistogramOnTarget(&histogram_params);

        if (rc == ASTL_STATUS_SUCCESS) {
            for (uint32_t i = 0; i < bin_count; ++i) {
                /* value union member matches the metric's native value type */
                printf("value=%" PRIu64 "  count=%u\n",
                       bins[i].value.ui64, bins[i].count);
            }
        }
        free(bins);
    }
} else if (rc == ASTL_STATUS_NOT_SUPPORTED) {
    puts("metric type does not support discrete histogram");
}
```

### `astl_discrete_histogram_bin_t` Fields

| Field   | Type           | Notes                                                                                      |
| ------- | -------------- | ------------------------------------------------------------------------------------------ |
| `size`  | `size_t`       | **Must** be set to `sizeof(astl_discrete_histogram_bin_t)` on the first array element.     |
| `value` | `astl_value_t` | The exact sampled value for this bin. Union member matches the metric's native value type. |
| `count` | `uint64_t`     | Number of samples whose value exactly equals `value`.                                      |

### Discrete Histogram Status Codes

#### `astlGetMetricDiscreteHistogramBinCountOnTarget`

| Code                        | Meaning                                                           |
| --------------------------- | ----------------------------------------------------------------- |
| `ASTL_STATUS_SUCCESS`       | `bin_count` set to the number of unique values (0 if no samples). |
| `ASTL_STATUS_BAD_ARGUMENT`  | A pointer argument is `NULL`.                                     |
| `ASTL_STATUS_NOT_SUPPORTED` | Metric type not supported by the discrete histogram summarizer.   |

#### `astlGetMetricDiscreteHistogramOnTarget`

| Code                             | Meaning                                                                    |
| -------------------------------- | -------------------------------------------------------------------------- |
| `ASTL_STATUS_SUCCESS`            | Bins filled successfully.                                                  |
| `ASTL_STATUS_BAD_ARGUMENT`       | A pointer argument is `NULL`, or `bin_count` is `0` on entry.              |
| `ASTL_STATUS_OLD_STRUCT_VERSION` | `bins[0].size` is smaller than `sizeof(astl_discrete_histogram_bin_t)`.    |
| `ASTL_STATUS_NEW_STRUCT_VERSION` | `bins[0].size` is larger than `sizeof(astl_discrete_histogram_bin_t)`.     |
| `ASTL_STATUS_NOT_SUPPORTED`      | Metric type not supported.                                                 |
| `ASTL_STATUS_BUFFER_TOO_SMALL`   | Array capacity < required bin count; `bin_count` updated to required size. |

---

## Timestamp Filtering

All sample-retrieval and summary APIs accept two timestamp filter fields that restrict which
collected samples contribute to the result:

| Field      | Type       | Meaning                                                                                                   |
| ---------- | ---------- | --------------------------------------------------------------------------------------------------------- |
| `start_ts` | `uint64_t` | If non-zero, only samples with `timestamp >= start_ts` are included. Uses `CLOCK_MONOTONIC_RAW` on Linux. |
| `end_ts`   | `uint64_t` | If non-zero, only samples with `timestamp <= end_ts` are included. Uses `CLOCK_MONOTONIC_RAW` on Linux.   |

Setting both fields to `0` (the default when using `ASTL_INIT_STRUCT`) disables filtering and
includes all collected samples.

### Clock Source

All sample timestamps are expressed in nanoseconds on the **`CLOCK_MONOTONIC_RAW`** clock
(`clock_gettime(CLOCK_ID_MONOTONIC_RAW, ...)`). Regardless of the underlying hardware
collector's native clock, ASTL converts every timestamp to `CLOCK_MONOTONIC_RAW` before storing
the sample. Use the same clock when computing the filter bounds:

```c
#include <time.h>
#include <stdint.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
```

### APIs That Support Filtering

The following APIs honour `start_ts` / `end_ts`:

- `astlGetCounterSampleCountOnTarget`
- `astlGetCounterSamplesOnTarget`
- `astlGetMetricSampleCountOnTarget`
- `astlGetMetricSamplesOnTarget`
- `astlGetMetricStatisticsOnTarget`
- `astlGetMetricDiscreteHistogramBinCountOnTarget`
- `astlGetMetricDiscreteHistogramOnTarget`

### Usage Example

```c
#include "astl/astl_telemetry.h"
#include <stdint.h>

enum { SAMPLE_CHUNK_CAPACITY = 4 };

static astl_status_code read_metric_samples_in_chunks(astl_target_handle_t target_handle,
                                                      astl_metric_handle_t metric_handle) {
    uint32_t expected_samples = 0;
    ASTL_INIT_STRUCT(astl_get_metric_sample_count_on_target_params_t, count_params,
        .flags         = 0,
        .target_handle = target_handle,
        .metric_handle = metric_handle,
        .sample_count  = &expected_samples,
        .start_ts      = 0,
        .end_ts        = 0);
    astl_status_code rc = astlGetMetricSampleCountOnTarget(&count_params);
    if (rc != ASTL_STATUS_SUCCESS) {
        return rc;
    }

    uint64_t      next_start_ts = 0;
    uint32_t      total_written = 0;
    astl_sample_t chunk[SAMPLE_CHUNK_CAPACITY];
    while (total_written < expected_samples) {
        uint32_t chunk_count = SAMPLE_CHUNK_CAPACITY;
        ASTL_INIT_STRUCT(astl_get_metric_samples_on_target_params_t, sample_params,
            .flags         = 0,
            .target_handle = target_handle,
            .metric_handle = metric_handle,
            .samples       = chunk,
            .sample_count  = &chunk_count,
            .start_ts      = next_start_ts,
            .end_ts        = 0);
        rc = astlGetMetricSamplesOnTarget(&sample_params);
        if (rc != ASTL_STATUS_SUCCESS || chunk_count == 0) {
            return rc;
        }
        total_written += chunk_count;
        next_start_ts = chunk[chunk_count - 1].timestamp + 1;
    }
    return ASTL_STATUS_SUCCESS;
}

static astl_status_code read_counter_samples_in_chunks(astl_target_handle_t target_handle,
                                                       astl_counter_handle_t counter_handle) {
    uint32_t expected_samples = 0;
    ASTL_INIT_STRUCT(astl_get_counter_sample_count_on_target_params_t, count_params,
        .flags          = 0,
        .target_handle  = target_handle,
        .counter_handle = counter_handle,
        .sample_count   = &expected_samples,
        .start_ts       = 0,
        .end_ts         = 0);
    astl_status_code rc = astlGetCounterSampleCountOnTarget(&count_params);
    if (rc != ASTL_STATUS_SUCCESS) {
        return rc;
    }

    uint64_t      next_start_ts = 0;
    uint32_t      total_written = 0;
    astl_sample_t chunk[SAMPLE_CHUNK_CAPACITY];
    while (total_written < expected_samples) {
        uint32_t chunk_count = SAMPLE_CHUNK_CAPACITY;
        ASTL_INIT_STRUCT(astl_get_counter_samples_on_target_params_t, sample_params,
            .flags          = 0,
            .target_handle  = target_handle,
            .counter_handle = counter_handle,
            .samples        = chunk,
            .sample_count   = &chunk_count,
            .start_ts       = next_start_ts,
            .end_ts         = 0);
        rc = astlGetCounterSamplesOnTarget(&sample_params);
        if (rc != ASTL_STATUS_SUCCESS || chunk_count == 0) {
            return rc;
        }
        total_written += chunk_count;
        next_start_ts = chunk[chunk_count - 1].timestamp + 1;
    }
    return ASTL_STATUS_SUCCESS;
}
```

### Filter Status Codes

If either filter field is non-zero, the API currently returns
`ASTL_STATUS_NOT_IMPLEMENTED`. Full filtering support will be added in a future release.

---

## Crop Samples API

`astlCropSamples` and `astlCropSamplesOnTarget` permanently reduce the in-memory sample set to
only those samples whose timestamp falls within a caller-supplied time window.

### Overview

| Function                  | Scope                                                                                                       |
| ------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `astlCropSamples`         | All configured targets and all collected counters/metrics (returns `ASTL_STATUS_NOT_IMPLEMENTED` currently) |
| `astlCropSamplesOnTarget` | A single target (returns `ASTL_STATUS_NOT_IMPLEMENTED` currently)                                           |

After cropping, all subsequent calls to sample-retrieval, summary, and histogram APIs operate on
the reduced dataset. If no samples remain, data APIs return `ASTL_STATUS_NO_DATA_COLLECTED`.

### Structs

#### `astl_crop_window_t`

Describes a single inclusive time window.

| Field      | Description                                                                                                                    |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `size`     | Must be `sizeof(astl_crop_window_t)`. Only `windows[0].size` is checked.                                                       |
| `flags`    | Reserved, must be `0`.                                                                                                         |
| `start_ts` | Inclusive window start (nanoseconds, `CLOCK_MONOTONIC_RAW`). `0` = no lower bound.                                             |
| `end_ts`   | Inclusive window end (nanoseconds, `CLOCK_MONOTONIC_RAW`). `0` = no upper bound. Must be `>= start_ts` when both are non-zero. |

#### `astl_crop_samples_params_t`

| Field          | Description                                                               |
| -------------- | ------------------------------------------------------------------------- |
| `size`         | Must be `sizeof(astl_crop_samples_params_t)`.                             |
| `flags`        | Reserved, must be `0`.                                                    |
| `windows`      | Pointer to caller-allocated `astl_crop_window_t` array. Cannot be `NULL`. |
| `window_count` | Number of elements in `windows`. Must be ≥ 1.                             |

#### `astl_crop_samples_on_target_params_t`

Same as `astl_crop_samples_params_t` with an additional `target_handle` field specifying the target to crop.

### Status Codes

| Code                                 | Meaning                                                                       |
| ------------------------------------ | ----------------------------------------------------------------------------- |
| `ASTL_STATUS_SUCCESS`                | Crop applied successfully.                                                    |
| `ASTL_STATUS_COLLECTION_NOT_STOPPED` | A target is still in STARTED or PAUSED state.                                 |
| `ASTL_STATUS_NOT_IMPLEMENTED`        | `astlCropSamples` and `astlCropSamplesOnTarget` are not yet implemented.      |
| `ASTL_STATUS_BAD_ARGUMENT`           | `params` or `windows` is `NULL`, `window_count` is 0, or `start_ts > end_ts`. |
| `ASTL_STATUS_INVALID_FLAG_VALUE`     | `params->flags != 0` or any window has `flags != 0`.                          |
| `ASTL_STATUS_OLD_STRUCT_VERSION`     | `params->size` or `windows[0].size` is smaller than expected.                 |
| `ASTL_STATUS_NEW_STRUCT_VERSION`     | `params->size` or `windows[0].size` is larger than expected.                  |

---

## Output Formats

ASTL supports multiple output mechanisms for processed telemetry samples:

1. Buffer Output (in-memory)

- Samples are written into a caller-provided contiguous buffer via `BufferOutput`.
- Use when integrating directly with a higher-level runtime (e.g., Python wrapper) or when you want zero file IO.
- Capacity mismatch semantics are reflected through status codes (e.g., `ASTL_STATUS_BUFFER_TOO_SMALL`).
- Suitable for low-latency pipelines or streaming directly into analytics code.

2. Perfetto JSON Trace Output (Visualization)

- Opt-in: only produced if the environment variable `ASTL_OUTPUT_PERFETTO` is set to a file path.
- Deferred emission: The variable is evaluated during `StopCollection` after metrics are summarized. No file is opened during sampling; the complete trace is written once at the end.
- Enable with:

  ```bash
  export ASTL_OUTPUT_PERFETTO=/tmp/astl_trace.json
  ```

- Emits a single JSON array; each sample becomes one event object.
- Numeric values (integral or floating) → counter events (`ph:"C"`). String values → instant events (`ph:"I"`, thread scope `s:"t"`).
- Stable `pid` per target; distinct `tid` per metric under that target for separate tracks.
- Category/unit mapping (selected): WATTS → Power, JOULES → Energy, CELSIUS → Temperature, MHZ → Frequency,
  VOLTS → Voltage, AMPS → Current, BYTES → Bytes, MBYTESPERSEC → Bandwidth, TICKS → Ticks, SECONDS → Time.
- String sample without quantitative unit → State; fallback → (empty category string).
- Name sanitization (whitespace & quotes → `_`); string values safely JSON-escaped.
- Compatible with Perfetto UI imports (Chrome Trace Event viewer also accepts similar structure; Perfetto is primary target).
- Skips null targets/metrics and empty sample vectors silently.
- Large runs can produce large files; consider compression or slicing or enabling compression post-generation.

```jsonc
{
  "ph": "C",
  "cat": "Power",

  "name": "SoC.Power",

  "ts": 1234567,
  "pid": 1,
  "tid": 1,
  "args": { "target": "SoC", "metric": "Power", "value": 3.14 },
}
```

- Import via Perfetto UI (<https://ui.perfetto.dev>) using "Open trace file".
  Choose Buffer Output for programmatic consumption; choose Perfetto for timeline visualization and exploratory analysis.

3. Interval CSV Output (Post-Collection Time Series)
   - Opt-in: produced only if the environment variable `ASTL_OUTPUT_INTERVAL_CSV` is set to a file path
     before `StopCollection`.
   - Deferred emission: identical lifecycle to Perfetto; CSV file is written once after metrics are
     summarized (no incremental writes during sampling).
   - Enable with:

     ```bash
     export ASTL_OUTPUT_INTERVAL_CSV=/tmp/astl_intervals.csv
     ```

   - ATX-compatible interval format: one section is written per metric/target pair.
     Each section begins with `metric_name on target_name`, followed by `timestamp_us,value`
     and then one row per sample. Sections are emitted in stable alphabetical order by metric name,
     then target name. Blank line separates sections.
   - Samples: `timestamp_us` is raw microseconds (same base as Perfetto). Value is numeric.
   - Empty collection yields collection metadata only.

Example snippet (two metrics, one sample each):

```csv
SoC Power on SoC

timestamp_us,value
1734735123456789,3.14

SoC Temp on SoC

timestamp_us,value
1734735124456790,55.0
```

Import into Python:

```python
import pandas as pd
df = pd.read_csv("/tmp/astl_intervals.csv")
```

4. Summary CSV Output (Post-Collection Statistical Summaries)
   - Opt-in: produced only if the environment variable `ASTL_OUTPUT_SUMMARY_CSV` is set to a file path
     before `StopCollection`.
   - Deferred emission: same lifecycle as Perfetto and Interval CSV; the file is written once after collection
     completes (no incremental writes during sampling).
   - Enable with:

     ```bash
     export ASTL_OUTPUT_SUMMARY_CSV=/tmp/astl_summary.csv
     ```

   MinMaxAvg CSV Table
   - Format: One header line then one row per (metric, target) pair summarizing all samples gathered for that pair.

   - Columns:

   - `MetricName` – metric name (sanitized where necessary)
   - `Target` – target name
   - `Min` – minimum numeric sample value (or `N/A` if no numeric samples)
   - `Max` – maximum numeric sample value (or `N/A` if no numeric samples)
   - `Average` – arithmetic mean of numeric samples (or `N/A` if no numeric samples)
   - `SampleCount` – total sample count (numeric + string) collected for the metric/target pair

   - Header: `MetricName,Target,Min,Max,Average,SampleCount`.
   - Grouping: Rows are grouped by metric name internally; current implementation does not insert blank lines between groups (compact listing).
   - Value Handling:
     - Min/max/avg summary values are only produced when the metric is of type `ASTL_METRIC_VALUE`,
       `ASTL_METRIC_DELTA`, or `ASTL_METRIC_RATE`.
     - If a metric produced only string samples (no numeric values), Min/Max/Average are `N/A`.
     - Numeric formatting matches the internal `to_string` representation (no forced scientific notation).
   - Empty collection yields an empty file (no header).
     - Rationale: Provides a quick, space-efficient statistical overview of all collected metrics to support
       rapid triage and selection of metrics for deeper time-series analysis (Interval CSV or Perfetto). Often
       significantly smaller than full interval dumps for long runs.

Example snippet:

```csv
MetricName,Target,Min,Max,Average,SampleCount
SoC Power,SoC,2.91,3.42,3.14,150
SoC Temp,SoC,44.0,57.0,52.3,150
Cluster0 Freq,Cluster0,900.0,1500.0,1200.5,150
GPU State,GPU,N/A,N/A,N/A,12

```

Histogram CSV Table

- Format: one header line, then one row per bin for each (metric, target) pair.

- Columns:
  `MetricName` – metric name (sanitized where necessary)
  `Target` – target name
  `Type` – `discrete` or `ranged` histogram type.
  `Value/Range` – numeric range. For `discrete`, the exact value.
  `Count` – number of samples falling in this bin.

- Header: `MetricName,Target,Type,Value/Range, Count`.
  Grouping: Rows are grouped by metric name internally; current implementation does not insert blank lines between groups (compact listing).
  Value Handling:
- Discrete histograms are only produced when the metric is of type ASTL_METRIC_VALUE, ASTL_METRIC_FINITE_SET_VALUE or ASTL_METRIC_EVENT
- They are appropriate when the samples yield non-rangeable categories (categorical data, small integer ranges, or finite sets).
- Each unique value becomes a bin.

Example snippet:

```csv
MetricName,Target,Type,Value,Count
CPU State,TLM_0,discrete,Idle,200
CPU State,TLM_0,discrete,Busy,300
CPU State,TLM_1,discrete,Idle,100
CPU State,TLM_1,discrete,Busy,400

```

Import into Python:

```python
import pandas as pd
df_summary = pd.read_csv("/tmp/astl_summary.csv")
```

## Running MockScmi

To run MockScmi:

1. **Create a Mount Directory**

Create a directory to serve as the mount point (e.g., `/tmp/scmi`):

```sh
mkdir -p /tmp/scmi
```

2. **Run MockScmi**

```sh
ASTL/build/debug/bin/MockScmi /tmp/scmi
```

### Optional Flags

- Display help message: -h
- Single-threaded operation: -s
- Run in foreground: -f

3. **Terminating MockScmi**

- Foreground mode: Simply press Ctrl+C to exit.
- Background mode: kill -SIGINT \<PID\>

## Build steps for developers

### Third-Party Components

ASTL incorporates the following third-party libraries:

- **[tinyexpr++](https://github.com/Blake-Madden/tinyexpr-plusplus)** - C++20 mathematical expression parser used for formula evaluation
  - Supports arithmetic, bitwise, and mathematical operations
  - Header-only library with minimal dependencies
  - Licensed under zlib/libpng license
  - Used for the Formula feature to transform raw metric values

### Compile and test

These commands will generate a workspace under `build` with auto-detected reasonable default
build systems and compilers, build it, and execute tests. Supported presets are found in
[CMakePresets.json](CMakePresets.json).

```sh
cmake -S . --preset debug
cmake --build --preset debug
ctest --preset debug
```

Procfs collection and its target-specific metric discovery are included by
default. Developers can omit that support from a build with:

```sh
cmake -S . --preset debug -DASTL_PROCFS=OFF
```

### Development hooks

ASTL uses the [pre-commit](https://pre-commit.com/) framework to format staged
files and run local quality checks.

To enable this, ensure you have the following dev tools installed:

- `pre-commit`
- the [Qlty CLI](https://docs.qlty.sh/cli/quickstart)
- `clang-format`
- `cmakelang`
- `cppcheck`
- REUSE 3.3 or later
- `jq`.

Configure the debug preset once so that cppcheck can
`build/debug/compile_commands.json`, then install the hooks:

```sh
cmake -S . --preset debug
./scripts/hooks/install_hooks.sh
```

The installer selects the Arm-Debug configuration only when `origin` is
`github.com/Arm-Debug/ASTL`; all other clones use the public configuration. Use
`--internal` or `--public` to override detection for a fork or unusual remote
layout.

Before each commit, staged files are formatted and re-staged, then Qlty and the
license checks run. Cppcheck runs when staged C or C++ source or header files
change. After those checks pass, the commit-message hook adds a
`Pre-Commit-Ran: true` trailer. Pull requests report non-blocking warnings for
non-merge commits without this trailer to help the team identify checkouts that
have not installed the hooks. A bot comment makes the warning visible in the
pull request conversation and is removed once every non-merge commit has the
trailer. The Arm-Debug configuration also installs
ossmosis staged-content and commit-message checks. Without the confidential
overlay, ossmosis uses its zero-config public policy. If the overlay later
supplies `.ossmosis.json`, the next commit automatically uses that policy
without reinstalling the hooks. The public configuration never downloads or
runs ossmosis.

Run every public hook manually with:

```sh
pre-commit run --config .pre-commit-config.yaml --all-files
```

For an Arm-Debug checkout, substitute
`.pre-commit-config-arm-debug.yaml`. Run one hook by appending its id, or skip
selected hooks for one commit with a comma-separated list such as
`SKIP=cppcheck,license-lint git commit`. To uninstall, run:

```sh
pre-commit uninstall --hook-type pre-commit
pre-commit uninstall --hook-type commit-msg
```

### Compile and test with specific compiler or build type

If you want to choose a specific compiler that's not specified in `CMakePresets.json`, you
can add arguments in the first configure step (be sure to set `EXPORT_COMPILE_COMMANDS` so
that clang-tidy can find system headers for linting).

```sh
cmake -B ./build/debug -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=DEBUG -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S .
cmake --build ./build/debug --config DEBUG
cd ./build/debug && ctest
```

### Custom targets

#### Formatting

To use `clang-format` to check formatting, use
[scripts/check_format.sh](scripts/check_format.sh) or the `cmake` target `check_format`.

```sh
cd build && cmake --build . --target check_format
# or
cd build && make check_format
```

To use `clang-format` to format code, use [scripts/format.sh](scripts/format.sh) or the target
`format`.

```sh
cd build && cmake --build . --target format
# or
cd build && make format
```

#### Linting

To use `clang-tidy` to lint code, use [scripts/lint.sh](scripts/lint.sh) or the target `lint`

```sh
cd build && cmake --build . --target lint
# or just
cd build && make lint
```

#### Doxygen

Automatically generate class diagrams, function call graphs, and API reference docs:

```sh
cd build/debug && cmake --build . --target doxygen
```

Output will be at: `<ASTL>/doc/html/index.html`.

Note: you need to install both doxygen and dot on your system:

```sh
sudo apt-get -y install doxygen graphviz
```

## Experimental Python API and usage

For Python usage (installation, quick start, streaming, diagnostics, derived metrics,
benchmarking) jump directly to the **[Python User Guide](python/doc/USER_GUIDE.md)**.

Python examples (including an end-to-end session + streaming + derived rates) are in
`python/samples/` and documented in the
**[User Guide](python/doc/USER_GUIDE.md#putting-it-together-end-to-end-example)**.

### Python API Documentation (Sphinx)

The Python layer includes a Sphinx scaffold under `python/doc/`.

#### Build HTML Docs

```bash
# (Optional) create / activate a virtual environment
python -m pip install --upgrade pip
python -m pip install sphinx

# From repository root
sphinx-build -b html python/doc python/doc/_build/html

# Open the generated documentation
xdg-open python/doc/_build/html/index.html 2>/dev/null \
  || open python/doc/_build/html/index.html \
  || echo "Docs at python/doc/_build/html/index.html"
```

### Incremental Rebuild During Editing

```bash
sphinx-build -b html -a -E python/doc python/doc/_build/html
```

### Adding New Modules

Add a new `api/<module>.rst` with:

```rst
:members:
:undoc-members:
:show-inheritance:
```

Then reference it in `index.rst` under the `.. toctree::`.

For richer themes:

```bash
python -m pip install sphinx_rtd_theme
```

Add to `conf.py`:

```python
html_theme = 'sphinx_rtd_theme'
```

## Python Packaging Notes

Python packaging uses this include resolution order in `python/setup.py`:

1. Repository headers: `<repo>/include`
2. Vendored fallback: `<repo>/python/astl/include`

For local editable installs in this repo, vendoring is usually not required because `<repo>/include`
exists. For sdist/wheel builds in isolated environments (where repo headers are unavailable), vendor
headers first:

```bash
python/scripts/vendor_headers.sh
```

What `vendor_headers.sh` does:

1. Ensures generated `astl_version.h` exists (configures/builds minimal targets if needed).
2. Copies public ASTL headers into `<repo>/python/astl/include/astl`.
3. Excludes internal `astl_test_hooks.h`.
4. Overwrites previously vendored headers to keep them current.

Optional:

```bash
python/scripts/vendor_headers.sh --build-dir build/debug
```

## Preparing Release Metadata

Stable releases use two manually initiated phases:

1. Dispatch the **Prepare Release** workflow from `main` for a major or minor
   release, or from an existing `release/` branch for a patch. Select the desired
   bump type and, if needed, the intended release date. The workflow validates the
   candidate, promotes `Unreleased`, updates `VERSION.md`, and opens a
   release-preparation PR containing both files.
2. Review and merge that PR through the normal CI and branch-protection process.
3. Dispatch the **Release** workflow from the merged target branch with release
   type `STABLE`. It verifies the prepared metadata, builds and tests that exact
   commit, creates the stable tag and artifacts, and publishes the release.

### Normal Stable Release Flow

Start with **Prepare Release** when the team decides to cut a normal major or
minor release from `main`. The diagram distinguishes actions taken by a release
manager or reviewer from work performed by GitHub Actions and the release
scripts.

![Normal stable release workflow](doc/design/release_process.svg)

[View the Mermaid source](doc/design/release_process.mmd).

Until the preparation commit is recorded as an immutable release candidate,
dispatch **Release** promptly after merging the preparation PR and before other
changes merge into `main`. The stable release workflow validates and publishes
the selected `main` revision.

When a stable release is published from `main`, the publishing workflow also
creates the permanent `release/VERSION` branch at the tagged commit and opens a
follow-up PR that advances `VERSION.md` to `VERSION.post`. Patch releases are
published from their existing release branch and do not modify `main`.

Scheduled and manually selected `ROLLING` releases remain single-phase: they
package the selected source revision without promoting the changelog.

To perform the first-phase metadata edit locally, run:

```bash
scripts/release/prepare_release.py --release-version 1.2.0
```

To validate an already-prepared tree without changing it, run:

```bash
scripts/release/prepare_release.py --release-version 1.2.0 --check
```

This script changes tracked metadata and is intended to run before the release
commit is reviewed and tagged unless `--check` is supplied. It does not build or
package ASTL.

## Staging a Release Locally

On a Linux host with the normal build prerequisites, build, test, and create the
public library release package with:

```bash
scripts/release/stage_release.sh
```

No source overlay or private repository is required. Use `--version` to stage a
different version without editing `VERSION.md`, `--output-dir` to choose the
artifact directory, or repeated `--variant` options to request other variants
when their corresponding tools are present. Run `--help` for the full interface.
Unlike `prepare_release.py`, `stage_release.sh` does not modify `CHANGELOG.md` or
`VERSION.md`; it builds, tests, and packages the source tree as it currently exists.

## Design Diagrams

Mermaid source (`.mmd`) and rendered SVG documentation diagrams live in `doc/design/`.
See `doc/design/DIAGRAMS.md` for:

- Diagram inventory and purposes
- Regeneration instructions (`node scripts/render_mermaid.js --all`)
- Optional vertical height compression for tall sequence diagrams via `SEQ_MAX_HEIGHT` environment variable
- CI drift detection snippet

Please regenerate diagrams whenever you change a `.mmd` file.
