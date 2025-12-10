# ASTL CLI Tool

## Overview

The ASTL CLI (`astl_cli`) is a command-line utility for detecting, displaying, collecting, and plotting hardware
telemetry metrics on Arm systems. It provides a user-friendly interface to the ASTL (Arm SoC Telemetry Library),
enabling users to:

- **Discover** available telemetry metrics and counters on the system
- **Collect** real-time telemetry data (power, temperature, frequency, etc.)
- **Visualize** collected metrics with integrated plotting capabilities
- **Monitor** system behavior during workload execution
- **Read** individual counter values on-demand

The tool supports multiple collection protocols including SCMI (System Control and Management Interface) and libsensors,
making it suitable for a wide range of Arm platforms.

## Dependencies

### Required Dependencies

- **ASTL Library**: The Arm SoC Telemetry Library must be built and available
- **C++20 Compiler**: GCC 10+ or Clang 12+ with C++23 support
- **CMake**: Version 3.20 or higher for building

### Optional Dependencies

- **gnuplot**: Required for plotting functionality (terminal, PNG, SVG outputs)

  ```bash
  # Ubuntu/Debian
  sudo apt install gnuplot

  # RHEL/CentOS/Fedora
  sudo dnf install gnuplot
  ```

### Third-Party Libraries (Included)

- **argparse**: Command-line argument parsing (header-only)
- **toml++**: TOML configuration file parsing (header-only)
- **gplot++**: C++ wrapper for gnuplot (header-only)

## Building

The ASTL CLI is built as part of the ASTL project:

```bash
cd /path/to/ASTL
cmake --preset debug  # or release
cmake --build --preset debug
```

The executable will be located at `build/debug/bin/astl_cli` (or `build/release/bin/astl_cli`).

## Basic Usage

### Command Structure

```bash
astl_cli [GLOBAL_OPTIONS] <COMMAND> [COMMAND_OPTIONS]
```

### Global Options

- `-c, --config-file <path>`: Path to TOML configuration file
- `-k, --astl-config-file <path>`: Path to ASTL configuration file (default: `~/.astl_configuration.json`)
- `-v, --verbose`: Enable verbose console output
- `--save-config <path>`: Write current configuration to TOML file and exit
- `-h, --help`: Display help information

### Available Commands

1. **list-metrics**: Display available telemetry metrics
2. **list-counters**: Display available hardware counters
3. **collect**: Collect and plot telemetry data
4. **read-counter**: Read a single counter value

## Commands and Examples

### 1. List Metrics

Display all available telemetry metrics on the system.

#### Basic Usage

```bash
# List all metrics with full details
./astl_cli list-metrics

# List only metric names
./astl_cli list-metrics --name-only

# Filter by units (e.g., only power and temperature metrics)
./astl_cli list-metrics --units-include-list WATTS --units-include-list CELSIUS
```

#### Options

- `--name-only`: Print only metric names (useful for scripting)
- `--units-include-list <unit>`: Filter metrics by unit type (repeatable)

#### Example Output

```bash
$ astl_cli list-metrics  --units-include-list WATTS --units-include-list CELSIUS
┌─────────────────┌────────────────────────────────┌─────────┌─────────────┐
│   Metric Name   │           Description          │  Units  │ Metric Type │
┌─────────────────┌────────────────────────────────┌─────────┌─────────────┐
│ ENERGY_COUNTER  │ SoC Power Consumption in Watts │ Watts   │ Rate        │
┌─────────────────┌────────────────────────────────┌─────────┌─────────────┐
│ SoC Temperature │ temp1                          │ Celsius │ Value       │
┌─────────────────┌────────────────────────────────┌─────────┌─────────────┐
│ CPU Power       │ power1                         │ Watts   │ Value       │
┌─────────────────┌────────────────────────────────┌─────────┌─────────────┐
│ IO Power        │ power2                         │ Watts   │ Value       │
└─────────────────└────────────────────────────────└─────────└─────────────┘
```

### 2. List Counters

Display all available hardware counters. Counters are the underlying hardware telemetry data sources.
Unlike metrics, ASTL doesn't apply any post-processing math on counters.
The tool passes the raw data up directly, maybe even omitting units.

#### Basic Usage

```bash
# List all counters with full details
./astl_cli list-counters

# List only counter names as a list, no table format
./astl_cli list-counters --name-only
```

#### Options

- `--name-only`: Print only counter names as a list, no table format

### 3. Collect Metrics

Collect telemetry data over a specified duration with plotting and optional workload execution.

#### Basic Usage

```bash
# Collect default metrics for 10 seconds at 100ms intervals, plot to terminal
./astl_cli collect -i 100 -d 10

# Collect specific metrics, plot to terminal
./astl_cli collect -i 100 -d 10 -m "SoC Power" -m "SoC Temperature"

# Collect with terminal plotting
./astl_cli collect -i 100 -d 10 -m "SoC Power" -p terminal

# Collect and save PNG plots
./astl_cli collect -i 100 -d 10 -m "SoC Power" -m "IO Power" -p png -o ./output/

# Collect while running a workload
./astl_cli collect -i 100 -d 10 -m "CPU Power" -- /path/to/benchmark --arg1 --arg2
```

#### Options

- `-i, --sampling-interval <ms>`: Collection sampling interval in milliseconds (default: 100)
- `-d, --duration <sec>`: Collection duration in seconds (default: 10)
- `-m, --metric <name>`: Metric name to collect (repeatable; use names from `list-metrics`)
- `-o, --output-dir <path>`: Output directory for plots (default: `./plots/`)
- `-p, --plot-type <type>`: Plot output type: `none`, `terminal`, `png`, or `svg` (default: `none`)
- `-- <command> [args...]`: Workload command and arguments to execute during collection

#### Example: Monitoring a Workload

```bash
# Monitor power and temperature while running stress test
./astl_cli collect \
  -i 50 \
  -d 60 \
  -m "SoC Power" \
  -m "CPU Power" \
  -m "SoC Temperature" \
  -p png \
  -o ./stress_results/ \
  -- stress-ng --cpu 8 --timeout 60s
```

### 4. Read Counter

Read the current value of a specific hardware counter.

#### Basic Usage

```bash
# Read a specific counter by name
./astl_cli read-counter "ENERGY_COUNTER_SoC Power"

# Using verbose mode for more details
./astl_cli -v read-counter "THROTTLE_EVENTS_Throttle Counts"
```

#### Arguments

- `counter-name`: Name of the counter to read (use `list-counters` to see available counters)

## Configuration Files

### CLI Configuration File (TOML)

A CLI configuration file allows you to set default values for commands and options, reducing the need for command-line arguments.

#### Example: `cli_config.toml`

```toml
# astl-cli configuration

# Default subcommand if none passed on CLI
command = "collect"

# Path to ASTL configuration file (optional)
astl_config_file = "/home/user/.astl_configuration.json"

# Enable verbose console output
verbose = false

[list-metrics]

# List of unit names to include
units-include-list = ["WATTS", "CELSIUS"]

# Print only metric names
name-only = false

[collect]

# Collection sampling interval in milliseconds
sampling_interval = 100

# Collection duration in seconds
duration = 10

# List of metrics (from list-metrics) to collect
metrics = ["SoC Power", "SoC Temperature", "CPU Power"]

# Output directory for plots
output-dir = "plots/"

# Plot output file type [none, terminal, png, or svg]
plot-type = "terminal"

# Workload program and arguments as array
workload = []

[list-counters]

# Print only counter names
name-only = false

[read-counter]

# Name of the counter to read
counter-name = ""
```

#### Using a Configuration File

```bash
# Use a specific config file
./astl_cli -c my_config.toml

# Generate a config file from current settings
./astl_cli --save-config my_config.toml collect -i 200 -d 30 -m "SoC Power"

# CLI arguments override config file settings
./astl_cli -c my_config.toml collect -d 60  # Override duration to 60 seconds
```

## Advanced Examples

### Example 1: Power Profiling During Compilation

```bash
./astl_cli collect \
  -i 50 \
  -d 300 \
  -m "SoC Power" \
  -m "CPU Power" \
  -m "IO Power" \
  -p png \
  -o ./build_power_profile/ \
  -- make -j8
```

### Example 2: Temperature Monitoring with Terminal Plot

```bash
./astl_cli collect \
  -i 100 \
  -d 60 \
  -m "SoC Temperature" \
  -m "CORE_TEMP" \
  -p terminal
```

### Example 3: Scripted Metric Collection

```bash
#!/bin/bash
# Collect metrics for multiple workloads

METRICS=("SoC Power" "SoC Temperature" "CPU Power")
WORKLOADS=("./benchmark1" "./benchmark2" "./benchmark3")

for workload in "${WORKLOADS[@]}"; do
    echo "Running ${workload}..."
    ./astl_cli collect \
        -i 50 \
        -d 120 \
        $(printf -- "-m '%s' " "${METRICS[@]}") \
        -p png \
        -o "./results/$(basename ${workload})/" \
        -- ${workload}
done
```

### Example 4: Export Configuration for Reproducibility

```bash
# Create a configuration file capturing your exact setup
./astl_cli \
  -k /path/to/astl_config.json \
  --save-config experiment_config.toml \
  collect \
  -i 100 \
  -d 30 \
  -m "SoC Power" \
  -m "Frequency" \
  -p svg

# Share the config file with colleagues
# They can reproduce your exact collection setup:
./astl_cli -c experiment_config.toml
```

## Troubleshooting

### No Metrics Available

- Verify ASTL configuration file exists and is valid JSON
- Check that `scmi_sysfs_telemetry_root_path` points to the correct location
- Ensure you have read permissions for the telemetry sysfs files
- Try running with `-v` (verbose) flag for more diagnostic information

### Plotting Not Working

- Verify gnuplot is installed: `gnuplot --version`
- Check that the output directory exists and is writable
- Ensure sufficient disk space for plot files

### Permission Denied Errors

- Some telemetry interfaces require elevated permissions
- Try running with `sudo` or configure appropriate udev rules
- Check file permissions on sysfs telemetry paths

### Counter or Metric Not Found

- Use `list-metrics` or `list-counters` to see available options
- Metric/counter availability depends on platform capabilities
- Check ASTL configuration file for proper metric definitions

## See Also

- [ASTL Main Documentation](../../README.md)
- [ASTL Design Documents](../../doc/design/)
- [Sample Configurations](../../samples/)

## License

Copyright (C) 2025 Arm Limited and/or its affiliates.  
Licensed under the Apache License, Version 2.0.
