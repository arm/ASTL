# ASTL Thermal Throttling Demo Guide

This guide explains how to run the ASTL Thermal Throttling scenario using the demo script and CSV simulation data.

## Overview

The ASTL (Arm SoC Telemetry Library) thermal throttling demo simulates real-world thermal management scenarios where a System-on-Chip (SoC) experiences temperature changes that trigger throttling mechanisms. This demo uses simulated throttling data from a CSV file to provide realistic telemetry readings.

## Prerequisites

1. **Build ASTL**: Ensure the ASTL project is compiled in debug mode:

   ```bash
   # From the ASTL root directory
   cmake --preset debug
   cmake --build build/debug
   ```

2. **Required Binaries**: The demo requires these executables to be built:

   - `MockSysfs`: FUSE-based mock SCMI sysfs generator
   - `sample_test`: ASTL sample application

3. **Dependencies**: Ensure you have the following installed:
   - FUSE libraries (for MockSysfs)

## Thermal Throttling Data

The thermal throttling simulation uses the CSV file located at:

```
tests/demo/data/SoC_Throttling_Simulation.csv
```

This CSV contains timestamped telemetry data with the following columns:

- **time_ms**: Timestamp in milliseconds
- **temperature_C**: Temperature in Celsius
- **throttle_count**: Number of throttling events accumulated.
- **energy_J**: Energy consumption in Joules
- **freq**: CPU frequency in MHz

Sample data format:

```csv
time_ms,temperature_C,throttle_count,energy_J,freq
100,40,5,500000,2000
200,40,5,500000,2000
300,40,5,500000,2000
...
```

## Running the Thermal Throttling Demo

### Basic Usage

To run the thermal throttling demo with the default CSV file:

```bash
# From the ASTL root directory
# Set the environment variable to point to default CSV file
export ASTL_MOCKSYSFS_CSV_FILE_PATH="./tests/demo/data/SoC_Throttling_Simulation.csv"
./scripts/demo.sh
```

By default, ASTL runs for 10 seconds with a 500 ms sampling interval, during which it:

- collects sampled‐value metrics for temperature and frequency
- derives delta metrics from cumulative thermal‐throttle counts
- computes power as a rate metric from the joules data

### Using Custom CSV File

To use a custom thermal simulation CSV file, set the `ASTL_MOCKSYSFS_CSV_FILE_PATH` environment variable:

```bash
# Set the environment variable to point to your custom CSV file
export ASTL_MOCKSYSFS_CSV_FILE_PATH="/path/to/your/thermal_data.csv"

# Run the demo
./scripts/demo.sh
```

### Demo Execution Modes

The demo script supports different execution modes:

#### 1. Immediate Mode (Default)

Collects a single immediate sample:

```bash
./scripts/demo.sh --immediate
```

#### 2. Interval Mode

Collects samples at regular intervals:

```bash
# Collect samples every 1000ms for 10 seconds (default)
./scripts/demo.sh --interval=1000 --duration=10

# Collect samples every 500ms for 30 seconds
./scripts/demo.sh --interval=500 --duration=30
```

## Understanding the Demo Output

When running the thermal throttling demo, you'll see:

1. **MockSysfs Startup**: The FUSE-based mock filesystem mounts at `~/tmp/fuse/arm_telemetry`
2. **SCMI Telemetry Structure**: Creates the mock SCMI telemetry hierarchy
3. **Sample Collection**: The `sample_test` application collects telemetry data
4. **Thermal Data**: Real-time simulation of thermal throttling events

### Expected Data Events

The thermal throttling demo exposes these telemetry data events:

| Event ID | Metric         | Unit    | Description                         |
| -------- | -------------- | ------- | ----------------------------------- |
| 0x7A9B   | Temperature    | Celsius | SoC temperature reading             |
| 0x8C3D   | Throttle Count | Count   | Number of thermal throttling events |
| 0x9E4F   | Energy         | Joules  | Energy consumption                  |
| 0x1A68   | Frequency      | MHz     | CPU operating frequency             |

## Directory Structure

During demo execution, the following structure is created:

```
~/tmp/fuse/arm_telemetry/
├── des/
│   ├── 0x7A9B/          # Temperature data event
│   ├── 0x8C3D/          # Throttle count data event
│   ├── 0x9E4F/          # Energy data event
│   └── 0x1A68/          # Frequency data event
├── tlm_enable
├── version
└── current_update_interval_ms
```

## Customizing Thermal Simulation Data

To create your own thermal throttling scenario:

1. **Create a CSV file** with the required columns:

   ```csv
   time_ms,temperature_C,throttle_count,energy_J,freq
   0,25,0,300000,3000
   1000,45,2,450000,2800
   2000,65,8,400000,2400
   3000,80,15,350000,2000
   4000,75,12,375000,2200
   ```

2. **Set the environment variable**:

   ```bash
   export ASTL_MOCKSYSFS_CSV_FILE_PATH="/path/to/your/custom_thermal.csv"
   ```

3. **Run the demo**:

   ```bash
   ./scripts/demo.sh --interval=1000 --duration=60
   ```

## Troubleshooting

### Common Issues

1. **MockSysfs not found**: Ensure you've built the project:

   ```bash
   cmake --build build/debug
   ```

2. **Permission denied for mount point**: Check FUSE permissions and try:

   ```bash
   sudo modprobe fuse
   ```

3. **CSV file not found**: Verify the path in `ASTL_MOCKSYSFS_CSV_FILE_PATH`:

   ```bash
   echo $ASTL_MOCKSYSFS_CSV_FILE_PATH
   ls -la "$ASTL_MOCKSYSFS_CSV_FILE_PATH"
   ```

### Log Files

Demo logs are written to:

- **MockSysfs log**: `$ASTL_ROOT/sysfs.log`
- **ASTL library logs**: Check console output

### Cleanup

The demo automatically cleans up on exit, but if needed, manually unmount:

```bash
# If MockSysfs is still running
killall MockSysfs

# If mount point is stuck
fusermount -u ~/tmp/fuse/arm_telemetry
```

## Environment Variables Reference

| Variable                       | Description                                              | Default Value                     |
| ------------------------------ | -------------------------------------------------------- | --------------------------------- |
| `ASTL_MOCKSYSFS_CSV_FILE_PATH` | Path to CSV file containing thermal simulation data      | `./SoC_Throttling_Simulation.csv` |
| `ASTL_LOG_LEVEL`               | ASTL library log level (trace, debug, info, warn, error) | info                              |
| `ASTL_LOG_NAME`                | Custom log file name                                     | (console output)                  |
| `ASTL_LOG_CONSOLE`             | Enable console logging (true/false)                      | true                              |

The `ASTL_LOG_*` environment variables are defined in [`utils/astl_logger.hpp`]. The ASTL logging system utilizes the spdlog library for high-performance, asynchronous logging capabilities.

While the demo is running, you can monitor the mock telemetry in another terminal:

```bash
# Watch temperature readings
watch -n 0.5 "cat ~/tmp/fuse/arm_telemetry/tlm-0/des/0x7A9B/value"

# Watch throttle count
watch -n 0.5 "cat ~/tmp/fuse/arm_telemetry/tlm-0/des/0x8C3D/value"
```

#### Viewing MockSysfs Log

To view the `sysfs.log` data written by MockSysfs use:

```bash
# Display MockSysfs logs
tail -f sysfs.log

## Integration with Other Tools

The thermal throttling demo can be integrated with:

- **Performance analysis tools**: Use the telemetry data for correlation with workload performance
- **Thermal modeling software**: Export the CSV data for thermal simulation validation
- **Monitoring dashboards**: Parse the ASTL output for dashboard statistics

For more information about ASTL library usage and API documentation, refer to the main [README.md](README.md).
```
