[![Integration](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml/badge.svg)](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml)
[![Maintainability](https://qlty.sh/badges/6f288530-d295-4eb4-a8d3-9fab05020fcb/maintainability.svg)](https://qlty.sh/gh/Arm-Debug/projects/ASTL)
[![Code Coverage](https://qlty.sh/badges/6f288530-d295-4eb4-a8d3-9fab05020fcb/coverage.svg)](https://qlty.sh/gh/Arm-Debug/projects/ASTL)
<a href="https://arm.app.blackduck.com/api/projects/bb3f58ac-a952-4b1c-8561-61a04d23bf57">
<img src="https://github.com/Arm-Debug/ASTL/actions/workflows/blackduck.yaml/badge.svg" alt="Blackduck"/>
</a>

# ASTL

Arm SoC Telemetry Library

# Description

ASTL is a self-contained library for SoC telemetry collection at Arm. It abstracts low level
interfaces to telemetry data sources on the system. Using a predefined API, a telemetry
collection tool or an AI framework can dynamically discover available supported telemetry on
the target platform, configure, start, (pause, resume), stop a collection and process
collected data. Collected data can be streamed directly to a user provided buffer or can be
written to a specified output file format, such as a Perfetto JSON file for data visualization.

The initial implementation focuses on the System Control and Management Interface (SCMI)
specification through the Linux SCMI sysfs interface. It may eventually be expanded to add
support for other interfaces such as: BIOS mailboxes, PCIe configuration spaces, direct
register accesses, MMIO, OS provided data or other sources of data.

The library has a C-interface for the API and a C++ implementation. A comprehensive experimental
Python wrapper layer (Cython bindings + high-level utilities) is now available—refer to the
**[Python User Guide](python/docs/USER_GUIDE.md)**.

# Key Goals and Properties

## Sharable

- New or other tools at Arm can use it (not used yet)
- Partners and external 3rd party tool developers can use it to access telemetry on Arm
  platforms (not used yet)

## Uniform

- Telemetry collection through fixed predefined set of API (not defined yet)

## Portable

- Rebuild on Windows or other OS’s with same user API interface (not ported yet)
- Wrap with python layer (not done yet)

## Extensible

- Driver to driver context-switch based collection (not implemented yet)
- SCMI specification extensions (not implemented yet)
- New/other platform level telemetry access mechanisms (not implemented yet)

## Reusable

- Can be deployed on all new platforms: IOT, Automotive, Client, Data center, GPUs, NPUs. (not deployed yet)
- Can be used by a telemetry collection tool or in an AI framework or directly to instrument a
  workload (not used yet)

# High Level Architecture Diagram

![image](https://github.com/user-attachments/assets/0cc5580e-eb22-4219-9118-adb486972032)

# Telemetry Collection Tool Usage Example Diagram

![image](https://github.com/user-attachments/assets/ee543a10-fae6-45a8-8305-7cce78a3521b)

# AI Framework Usage Example Diagram

![image](https://github.com/user-attachments/assets/e514cfb8-7d15-45f6-899e-2b70c2c6c5db)

# High level Internal Design and status

<img width="708" height="436" alt="image" src="https://github.com/user-attachments/assets/16aceb9e-b837-47fc-a2d9-e7fee2a3d236" />

# Testing and Isolation Methodology

![image](https://github.com/user-attachments/assets/0a2b1e39-cb08-4e04-9f62-bba5329bfe56)

- Complete isolation from all dependencies
- Mock mode SCMI sysfs file/folder generator
- Mock mode data generator

- Bench testing suite
- Trompleil and Catch2 unit testing
- Mock command line collector executable

# Installation and usage

## Installation

See [Build steps for developers](#build-steps-for-developers) for more detailed build instructions.

> Looking for the Python telemetry wrapper? See the
> **[ASTL Python User Guide](python/docs/USER_GUIDE.md)** for: initialization, streaming (sync &
> async), diagnostics CLI, derived metrics, DataFrame integration, benchmarking, and
> exception model.

## Usage

The complete flow is demonstrated in [`samples/sample_test.cpp`](samples/sample_test.cpp). Below are the minimal snippets you need:

```cpp
#include "astl/astl.h"          // core API

#include "astl_telemetry.h"     // Function calls
```

0. Mount the Sysfs interface:

mount -t stlmfs none /sys/fs/arm_telemetry/

````

1. Initialize ASTL

First, create or select an ASTL JSON configuration file specifying which metrics should be
made available at the API to collect. You can also optionally override the root path for the
SCMI file system, and the definition file for the system metrics.

```json
{
{
  "scmi_sysfs_telemetry_root_path": "/sys/fs/arm_telemetry",

  "metrics": {
    "SoC Temperature": {
      "description": "SoC Temperature in Celsius",
      "register": "CORE_TEMP_0",
      "unit": "C",
      "metric_type": "value",
      "collection_protocol": "scmi"
    },
    "SoC Power": {
      "description": "SoC Power Consumption in Watts",
      "register": "ENERGY_COUNTER",
      "unit": "W",
      "metric_type": "rate",
      "collection_protocol": "scmi"
    }
  },
  "scmi_specification_path": "./samples/sample_topology/example_scmi_specification.json"
}
}
````

Key elements of the configuration file:

1. metrics: a set of objects, each with a name as a key, along with the following fields:
1. description: use readable notes to explain the metric
   register: the exact name of the register where ASTL should read this metric's data from
   (e.g. a `layout/member` key in the SCMI spec)
1. unit: will identify which `astl_units_t` to associate with this metric
   metric_type - select the `astl_metric_type_t` to measure this data
   collection_protocol - select which collectors should try to measure this
   scmi_specification_path: optional override for the JSON file specifying the data event IDs
   and targets for each metric on the platform.

```cpp
ASTL_INIT_STRUCT(astl_initialization_parameters_t, init_params, ._configuration_file_path = "~/.my_astl_config.json");
auto status = astlInitialize(&init_params);
if (status != ASTL_STATUS_SUCCESS) {
    // handle error...
}
```

2. Discover targets

```cpp
status = astlGetTargetCount(&target_count);
// allocate an array to hold the properties of each target
ASTL_ALLOC_ARRAY(astl_target_properties_t, target_properties_buffer, target_count);
status = astlGetTargets(target_properties_buffer, &target_count);
...
// using the first target
astl_target_properties_t target_properties = target_properties_buffer[0];
```

3. Configure collection

```cpp
uint32_t metric_count{};
astlGetMetricCount(target_properties._handle, &metric_count);
std::vector<astl_metric_properties_t> metric_buffer(metric_count);
status = astlGetMetrics(target_properties._handle, metric_buffer.data(), &metric_count);

ASTL_INIT_STRUCT(astl_collection_parameters_t, collection_params,
    ._sampling_interval = 0,
    ._collection_mode   = ASTL_COLLECTION_MODE_IMMEDIATE,
    ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD,
);
status = astlConfigureMetricCollectionOnTarget(target_properties._handle, &collection_params,
                                                &metric_buffer.front()._handle, metric_count);
```

4. Start, read, and stop collection

```cpp
status = astlStartCollectionOnTarget(target_properties._handle);

status = astlReadImmediateOnTarget(target_properties._handle);
status = astlStopCollectionOnTarget(target_properties._handle);
```

5. Retrieve metric samples. Logs are written to:

- `sampled_value_raw.log` and `sampled_value_summary.log` - metric data
- sysfs.log - mock SCMI driver output

6. Clean up allocated resources

```cpp
ASTL_FREE_ARRAY(target_properties_buffer)
```

Run `scripts/demo.sh` to run this flow. This sets up the mock driver and performs a sample run.
To run manually, start a mock server and execute `build/debug/bin/sample_test`. Use `--help`
for usage details.

## Design Diagrams (Mermaid)

The detailed runtime and structural diagrams are authored in Mermaid (`doc/design/*.mmd`) and rendered to SVG via `node scripts/render_mermaid.js --all`.

Because extremely tall single sequence diagrams became unreadable when constrained to a
uniform viewport, the original monolithic system sequence was split into phased diagrams:

1. `system_phase_init_discovery.mmd` – Initialization & target/metric discovery
2. `system_phase_metric_config.mmd` – Metric configuration & operations derivation
3. `system_phase_collection.mmd` – Interval sampling loop, immediate reads, pause/resume stubs
4. `system_phase_stop_processing.mmd` – Deferred processing at stop & summarization
5. `system_phase_retrieval_shutdown.mmd` – Retrieval APIs, shutdown, representative errors

An overview diagram remains in `system_end_to_end_sequence.mmd` that references these phases
at a high level.

Regenerate all diagrams after editing any `.mmd` file:

```sh
node scripts/render_mermaid.js --all
```

## Running the Mock SCMI Sysfs Generator

To run the mock SCMI sysfs generator:

1. **Create a Mount Directory**

Create a directory to serve as the mount point (e.g., `/tmp/scmi`):

```sh
mkdir -p /tmp/scmi
```

2. **Run Mock Sysfs**

```sh
ASTL/build/debug/bin/MockSysfs /tmp/scmi
```

### Optional Flags

- Display help message: -h
- Single-threaded operation: -s
- Run in foreground: -f

3. **Terminating Mock Sysfs**

- Foreground mode: Simply press Ctrl+C to exit.
- Background mode: kill -SIGINT \<PID\>

# Build steps for developers

## Compile and test

These commands will generate a workspace under `build` with auto-detected reasonable default
build systems and compilers, build it, and execute tests. Supported presets are found in
[CMakePresets.json](CMakePresets.json).

```sh
cmake -S . --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Compile and test with specific compiler or build type

If you want to choose a specific compiler that's not specified in `CMakePresets.json`, you
can add arguments in the first configure step (be sure to set `EXPORT_COMPILE_COMMANDS` so
that clang-tidy can find system headers for linting).

```sh
cmake -B ./build/debug -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=DEBUG -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S .
cmake --build ./build/debug --config DEBUG
cd ./build/debug && ctest
```

## Custom targets

### Formatting

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

### Linting

To use `clang-tidy` to lint code, use [scripts/lint.sh](scripts/lint.sh) or the target `lint`

```sh
cd build && cmake --build . --target lint
# or just
cd build && make lint
```

### Doxygen

Automatically generate class diagrams, function call graphs,

# Experimental Python API and usage

For Python usage (installation, quick start, streaming, diagnostics, derived metrics,
benchmarking) jump directly to the **[Python User Guide](python/docs/USER_GUIDE.md)**.

Python examples (including an end-to-end session + streaming + derived rates) are in
`python/samples/` and documented in the
**[User Guide](python/docs/USER_GUIDE.md#putting-it-together-end-to-end-example)**.

## Python API Documentation (Sphinx)

The Python layer includes a Sphinx scaffold under `python/docs/`.

### Build HTML Docs

```bash
# (Optional) create / activate a virtual environment
python -m pip install --upgrade pip
python -m pip install sphinx

# From repository root
sphinx-build -b html python/docs python/docs/_build/html

# Open the generated documentation
xdg-open python/docs/_build/html/index.html 2>/dev/null \
  || open python/docs/_build/html/index.html \
  || echo "Docs at python/docs/_build/html/index.html"
```

### Incremental Rebuild During Editing

```bash
sphinx-build -b html -a -E python/docs python/docs/_build/html
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

and other documentation automatically from source.
You don't need to build or run first, only configure.
Output will be at: \<ASTL\>/doc/html/index.html

```sh
cd build/debug && cmake --build . --target doxygen
```

Note: you need to install both doxygen and dot on your system:

```sh
sudo apt-get -y install doxygen graphviz
```

## Python Packaging Notes

When publishing to PyPI or building wheels from an sdist in isolated environments, the Cython
extension must compile against the ASTL public C headers. Because the top-level `include/` tree
is not present inside an sdist extraction, a release process must first vendor those headers
into `python/astl/include/astl`.

Refresh vendored headers (including generated `astl_version.h`) before building a release:

```bash
python/scripts/vendor_headers.sh
```

This script:

1. Ensures a build directory exists (configuring CMake if needed) so `astl_version.h` is generated.
2. Copies public headers (excluding the template `astl_version.h.in`) and the generated
   `astl_version.h` into the Python package.
3. Overwrites any existing vendored headers to keep them current.

The `setup.py` logic will first look for the real repo `include/` path and fall back to the
vendored copy when necessary. This guarantees `pip wheel astl-<version>.tar.gz` succeeds without
the full repository.

## Design Diagrams

Mermaid source (`.mmd`) and rendered SVG documentation diagrams live in `doc/design/`.
See `doc/design/DIAGRAMS.md` for:

- Diagram inventory and purposes
- Regeneration instructions (`node scripts/render_mermaid.js --all`)
- Optional vertical height compression for tall sequence diagrams via `SEQ_MAX_HEIGHT` environment variable
- CI drift detection snippet

Please regenerate diagrams whenever you change a `.mmd` file.
