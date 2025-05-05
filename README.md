[![Clang-Tidy](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml/badge.svg)](https://github.com/Arm-Debug/ASTL/actions/workflows/integration.yml)
[![Test Coverage](https://api.codeclimate.com/v1/badges/f3a3a281741e77874f98/test_coverage)](https://codeclimate.com/repos/67fd746b1af68e00baef93ca/test_coverage)
# ASTL
Arm SoC Telemetry Library

# Description
ASTL is a self-contained library for SoC telemetry collection at Arm. It abstracts low level interfaces to telemetry data sources on the system. Using a predefined API, a telemetry collection tool or an AI framework can dynamically discover available supported telemetry on the target platform, configure, start, (pause, resume), stop a collection and process collected data. Collected data can be streamed directly to a user provided buffer or can be written to a specified output file format, such as perfeto json file for data visualization.

The initial implementation focuses on the System Control and Management Interface (SCMI) specification through the Linux SCMI sysfs interface. It may eventually be expanded to add support for other interfaces such as: BIOS mailboxes, PCIe configuration spaces, direct register accesses, MMIO, OS provided data or other sources of data.

The library has a C-interface for the API and a C++ implementation. There is also plan to offer a python wrapper interface (not implemented yet)

# Key Goals and Properties
## Sharable
- New or other tools at Arm can use it (not used yet)
- Partners and external 3rd party tool developers can use it to access telemetry on Arm platforms (not used yet)
## Uniform
- Telemetry collection through fixed predefined set of API (not defined yet)
## Portable
- Rebuild on Windows or other OS’s with same user API interface (not ported yet)
- Wrap with python layer (not done yet)
## Extensible:
- Driver to driver context-switch based collection (not implemented yet)
- SCMI specification extensions (not implemented yet)
- New/other platform level telemetry access mechanisms (not implemented yet)
## Reusable
- Can be deployed on all new platforms: IOT, Automotive, Client, Data center, GPUs, NPUs. (not deployed yet)
- Can be used by a telemetry collection tool or in an AI framework or directly to instrument a workload (not used yet)

# High Level Architecture Diagram
![image](https://github.com/user-attachments/assets/0cc5580e-eb22-4219-9118-adb486972032)

# Telemetry Collection Tool Usage Example Diagram
![image](https://github.com/user-attachments/assets/ee543a10-fae6-45a8-8305-7cce78a3521b)

# AI Framework Usage Example Diagram
![image](https://github.com/user-attachments/assets/e514cfb8-7d15-45f6-899e-2b70c2c6c5db)

# Testing and Isolation Methodology
![image](https://github.com/user-attachments/assets/0a2b1e39-cb08-4e04-9f62-bba5329bfe56)

- Complete isolation from all dependencies
- Mock mode SCMI sysfs file/folder generator
- Mock mode data generator
- Gtest API bench testing suite
- Mock command line collector executable

# Installation and usage

TBD

## Running the Mock SCMI Sysfs Generator

To run the mock SCMI sysfs generator:

```

1. **Create a Mount Directory**

Create a directory to serve as the mount point (e.g., `/tmp/scmi`):

```sh
mkdir -p /tmp/scmi
```

2. **Run Mock Sysfs**
```sh
ASTL/build/debug/bin/MockSysfs /tmp/scmi
```

**Some Optional Flags:**
- Display help message: -h
- Single-threaded operation: -s
- Run in foreground: -f

3. **Terminating Mock Sysfs**
- Foreground mode: Simply press Ctrl+C to exit.
- Background mode: kill -SIGINT \<PID\>

# Build steps for developers

## Initial clone and setup

We use vcpkg as a git submodule, so you need to clone with `--recursive`. If you already cloned and forgot that, you can get the submodule via 

```sh
git submodule update --init --recursive
./vcpkg/bootstrap-vcpkg.sh
```

## Compile and test

These commands will generate a workspace under 'build' with auto-detected reasonable default build systems and compilers, build it, and execute tests
Supported presets are found in [CMakePresets.json](CMakePresets.json)

```sh
cmake -S . --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Compile and test with specific compiler or build type

If you want to choose a specific compiler that's not specified in CMakePresets.json, you can add arguments in the first configure step.
(Be sure to set EXPORT_COMPILE_COMMANDS so that clang-tidy can find system headers for linting)

```sh
cmake -B ./build/debug -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=DEBUG -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S .
cmake --build ./build/debug --config DEBUG
cd ./build/debug && ctest
```

## Custom targets

### Formatting

To use `clang-format` to check formatting, use [scripts/check_format.sh](scripts/check_format.sh) or the `cmake` target `check_format`

```sh
cd build && cmake --build . --target check_format
# or
cd build && make check_format
```

To use `clang-format` to format code, use [scripts/format.sh](scripts/format.sh) or use the target `format`

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
