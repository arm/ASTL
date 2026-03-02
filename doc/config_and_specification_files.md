<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Configuration and Specification Files

This document describes the files in the [`config/`](../config/) folder, their origins, and usage.

## Table of Contents

- [SCMI Spec Files vs Metrics Declarations](#scmi-spec-files-vs-metrics-declarations)
- [Importing SCMI Spec Files](#importing-scmi-spec-files)
- [Platform Detection for SCMI Specification](#platform-detection-for-scmi-specification)
- [Telemetry Specification Files](#telemetry-specification-files)
- [Metrics Declaration Files](#metrics-declaration-files)
- [Publishing Files for Release](#publishing-files-for-release)

## SCMI Spec Files vs Metrics Declarations

**SCMI spec files** come from platform telemetry architects outside this repository (e.g., [Platform Telemetry JSON Factory](https://gitlab.prod.spe.aws.arm.com/Joko.Sastriawan/platform-telemetry-json-factory)). These files define what telemetry counters are available on a platform.

**Metrics declaration files** are our collection of metrics with processing logic that we present for a platform. They reference counters from SCMI spec files but add analysis on top—at minimum, adding units. Future metrics might compute averages, min/max values, or combinations of multiple counters (e.g., performance per watt).

## Importing SCMI Spec Files

Import [`config/scmi/alpha/`](../config/scmi/alpha/) and [`config/scmi/beta/`](../config/scmi/beta/) from the [Platform Telemetry JSON Factory](https://gitlab.prod.spe.aws.arm.com/Joko.Sastriawan/platform-telemetry-json-factory):

1. Rename `outputs_v1/public` to [`config/scmi/alpha/public`](../config/scmi/alpha/public)
2. Rename `outputs/` to [`config/scmi/beta/public`](../config/scmi/beta/public)
3. Exclude `outputs[_v1]/internal`

## Platform Detection for SCMI Specification

Each platform supports different sensors. ASTL first reads the telemetry source's UUID from the SCMI Sysfs interface. The file [`config/scmi/beta/public/repometa.json`](../config/scmi/beta/public/repometa.json) maps UUIDs to their specification files:

```json
{
  "last_updated": "2026-01-13",
  "uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2025-12-18",
      "description": "Mock SCMI Sysfs - Test harness for ASTL development",
      "specification_file": "mocksysfs/mockscmi.json",
      "confidential": true
    }
  }
}
```

## Telemetry Specification Files

The [`config/scmi/`](../config/scmi/) directory contains telemetry source specification files for the SCMI protocol. Both [`config/scmi/alpha/`](../config/scmi/alpha/) and [`config/scmi/beta/`](../config/scmi/beta/) are imported from the [Platform Telemetry JSON Factory](https://gitlab.prod.spe.aws.arm.com/Joko.Sastriawan/platform-telemetry-json-factory).

These files enumerate the in-band telemetry sensors accessible through SCMI.

### Single Sensor Example

```json
{
   "base_de_id": "0x00001A68",
   "name": "FREQUENCY",
   "component": "CORE",
   "description": "Mock sysfs cpu frequency",
   "unit": "MHz",
   "base10_unit_modifier": 0,
   "rel_offset": "0x0000"
},
```

This example defines a register named `FREQUENCY` for the `CORE` component with data event ID `0x1A68`.
With other metric definitions, and a automatically detected lm-sensors target,
we might see metrics with names like the table below:

```bash
$ ./build/debug/bin/astl_cli list-metrics
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│       Metric Name      │                      Description              │  Units  │ Metric Type │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ CORE.0.FREQUENCY       │ Current Frequency of the SoC in MHz           │ MHz     │ Value       │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ SOC.0.ENERGY_COUNTER   │ SoC Power Consumption in Watts                │ Watts   │ Rate        │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ CORE.0.THROTTLE_EVENTS │ Number of thermal throttling events           │ Unknown │ Delta       │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ SoC Temperature        │ Libsensors apm_xgene-isa-0000 SoC Temperature │ Celsius │ Value       │
└────────────────────────└───────────────────────────────────────────────└─────────└─────────────┘
```

### Multiple Instances

To reduce duplication for repeated sensors, a block can have multiple instances. The `count` field repeats each `member`, with the instance number occupying the most significant 16 bits of the data event ID.
In the example below, we see one counter defined for the SoC, and 2 counters defined for 2 cores each.
The instance id (counting from 0 up to 'count') becomes the high 16 bits of the data event id.

```json
{
  "count": 1,
  "start_offset": 0,
  "block_size": 32,
  "members": [
    {
      "base_de_id": "0x00001A60",
      "name": "ENERGY_COUNTER",
      "component": "SOC",
      "description": "SoC Power Consumption in Watts",
      "unit": "Watts",
      "base10_unit_modifier": 0,
      "rel_offset": "0x0000"
    }
  ]
},
{
  "count": 2,
  "start_offset": 32,
  "block_size": 64,
  "members": [
    {
      "base_de_id": "0x00001A68",
      "name": "FREQUENCY",
      "component": "CORE",
      "description": "Current Frequency of the Core in MHz",
      "unit": "MHz",
      "base10_unit_modifier": 0,
      "rel_offset": "0x0000"
    },
    {
      "base_de_id": "0x00001A72",
      "name": "THROTTLE_EVENTS",
      "component": "CORE",
      "description": "Number of thermal throttling events",
      "unit": "MHz",
      "base10_unit_modifier": 0,
      "rel_offset": "0x0000"
    }
  ]
}
```

This definition would imply two measurable counters for FREQUENCY for cores 0 and 1.
Essentially, ASTL will build metric names with <component>.<instance>.<name> where instance counts up
from 0 to the 'count' element containing these members.

```bash
$ ./build/debug/bin/astl_cli list-metrics
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│       Metric Name      │                      Description              │  Units  │ Metric Type │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ SOC.0.ENERGY_COUNTER   │ SoC Power Consumption in Watts                │ Watts   │ Rate        │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ CORE.0.FREQUENCY       │ Current Frequency of the Core in MHz          │ MHz     │ Value       │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ CORE.1.FREQUENCY       │ Current Frequency of the Core in MHz          │ MHz     │ Value       │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ CORE.0.THROTTLE_EVENTS │ Number of thermal throttling events           │ Unknown │ Delta       │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ CORE.1.THROTTLE_EVENTS │ Number of thermal throttling events           │ Unknown │ Delta       │
┌────────────────────────┌───────────────────────────────────────────────┌─────────┌─────────────┐
│ SoC Temperature        │ Libsensors apm_xgene-isa-0000 SoC Temperature │ Celsius │ Value       │
└────────────────────────└───────────────────────────────────────────────└─────────└─────────────┘
```

- `SOC.0.ENERGY_COUNTER` at DE_ID `0x0000_1A60`
- `CORE.0.FREQUENCY` at DE_ID `0x0000_1A68`
- `CORE.1.FREQUENCY` at DE_ID `0x0001_1A68`
- `CORE.0.THROTTLE_EVENTS` at DE_ID `0x0000_1A72`
- `CORE.1.THROTTLE_EVENTS` at DE_ID `0x0001_1A72`

### Scmi Specification Parsing Implementation

See the parser implementation in:

- [`src/impl/config/scmi_platform_telemetry_spec.hpp`](../src/impl/config/scmi_platform_telemetry_spec.hpp)
- [`src/impl/config/scmi_platform_telemetry_spec.cpp`](../src/impl/config/scmi_platform_telemetry_spec.cpp)

## Metrics Declaration Files

The metrics declaration files are created by the ASTL development team and define how available telemetry sensors are processed and presented.

### Platform Lookup

The file [`config/metrics/platform_lookup.json`](../config/metrics/platform_lookup.json) maps target UUIDs to metrics declarations, similar to [`config/scmi/*/repometa.json`](../config/scmi/):

```json
{
  "last_updated": "2025-12-18",

  "scmi_uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2025-12-18",
      "description": "Mock SCMI Sysfs - Test harness for ASTL development",
      "metrics_file": "mocksysfs/metrics.json",
      "confidential": true
    }
  }
}
```

When ConfigManager encounters the `CAFEBABE` UUID, it builds metrics using [`config/metrics/mocksysfs/metrics.json`](../config/metrics/mocksysfs/metrics.json).

### Metrics Declaration Format

Metrics declaration files (e.g., [`config/metrics/mocksysfs/metrics.json`](../config/metrics/mocksysfs/metrics.json)) specify which telemetry counters are available and their processing methods:

```json
  "metrics": {
    "SoC Temperature": {
      "description": "SoC Temperature in Celsius",
      "unit": "C",
      "metric_type": "value",
      "category": "TEMPERATURE",
      "metric_groups": ["thermal", "throttling"],
      "collection": {
        "register": "CORE_0_TEMP",
        "scmi_component_filter": "CORE",
        "scmi_instance_filter": "0",
        "protocol": "scmi"
      }
    },
    "Throttle Counts": {
      "description": "Number of thermal throttling events",
      "unit": "",
      "metric_type": "delta",
      "category": "COUNT",
      "metric_groups": ["throttling"],
      "collection": {
        "register": "THROTTLE_EVENTS",
        "protocol": "scmi"
      }
    },
```

#### Field Definitions

| Field                                | Description                                                                         | Example                        |
| ------------------------------------ | ----------------------------------------------------------------------------------- | ------------------------------ |
| **key**                              | Metric name in ASTL's metrics list                                                  | `"SoC Temperature"`            |
| **unit**                             | Scientific unit for the metric. If spec provides units, acts as filter for counters | `"W"`                          |
| **metric_type**                      | Processing method for the metric                                                    | `"value"`, `"residency"`, etc. |
| **metric_groups**                    | API metric groups that include this metric                                          | `["thermal"]`                  |
| **collection.register**              | Matches `member.name` in SCMI spec for data event ID lookup                         | `"THROTTLE_EVENTS"`            |
| **collection.protocol**              | Data collection protocol                                                            | `"scmi"`                       |
| **collection.scmi_component_filter** | Filters SCMI counters by matching the `component` field                             | `"PSS"`                        |

| **collection.scmi_instance_filter** | Selects specific instance of repeated counter by instance number | `"1"` |
| **formula** | Post-processing formula for raw data | `"value * 1000"` |

### Metrics Parsing Implementation

Metrics declaration files are parsed by:

- [`src/impl/config/metric_json_declaration.hpp`](../src/impl/config/metric_json_declaration.hpp)
- [`src/impl/config/metric_json_declaration.cpp`](../src/impl/config/metric_json_declaration.cpp)

## Publishing Files for Release

So we need a mechanism to select and move a subset of files and elements to an output directory. That's what [`scripts/publish_configs.sh`](../scripts/publish_configs.sh) is for.

```bash
./scripts/publish_configs.sh --help

 OUTPUT_DIR [OPTIONS]

Publish ASTL config directory with filtering and SCMI spec version selection.

Required arguments:
  -o OUTPUT_DIR             Output directory to copy config/ to (with modifications)

Optional arguments:
  --scmi-spec-version VER   SCMI spec version to use: 'alpha' or 'beta' (default: beta)
  --confidential            Include confidential content (default: exclude confidential content)
  --mocksysfs               Include mock scmi sysfs metrics and scmi spec for testing
  -h, --help                Show this help message

Examples:

  # Publish beta spec for non-confidential/public use
  ./scripts/publish_configs.sh -o /path/to/output --scmi-spec-version beta

  # Publish alpha spec including confidential content
  ./scripts/publish_configs.sh -o /path/to/output --scmi-spec-version alpha --confidential
```

Our demo.sh script uses publish_configs.sh to set up the config files in the build/debug/lib directory today.
