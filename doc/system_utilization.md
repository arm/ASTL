<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# System utilization

The procfs collector exposes CPU and physical-memory utilization through the
`sysutil` target. Select this backend with `ASTL_COLLECTORS=procfs` and set
`ASTL_CONFIG_DIR` only when replacing the bundled configuration. The C, Python, and Go
discovery APIs expose the same names; no additional API calls are required.

## Migration

The previous target and utilization metric names are removed without aliases.
Update application target lookups, metric filters, and ATX or AMX configurations.

| Item                        | Previous name           | New name                |
| --------------------------- | ----------------------- | ----------------------- |
| Target                      | `procfs`                | `sysutil`               |
| Aggregate CPU utilization   | `stat.cpu.utilization`  | `cpu.total.utilization` |
| Logical CPU N utilization   | `stat.cpuN.utilization` | `cpu.N.utilization`     |
| Total physical memory       | `meminfo.MemTotal`      | `memory.total`          |
| Used physical memory        | `meminfo.MemUsed`       | `memory.used`           |
| Physical-memory utilization | `meminfo.utilization`   | `memory.utilization`    |

Collector selection remains `ASTL_COLLECTORS=procfs`. Other raw procfs metrics
retain their existing names. Metric IDs remain backend-specific; applications
should discover metrics by name rather than construct IDs.

## Units and sampling

CPU percentages use float64 values in the `cpu` group. Aggregate utilization is
`cpu.total.utilization`; `cpu.N.utilization` measures logical processor N, not
necessarily a physical core. Utilization is derived from changes in `/proc/stat`
CPU times between samples, with idle plus iowait counted as idle. Two samples are
required to calculate a percentage.

Memory metrics belong to the `memory` group. `memory.total` and `memory.used`
are uint64 byte values. Used memory is `MemTotal - MemAvailable` from
`/proc/meminfo`; `memory.utilization` is float64 percent, calculated as
`100 * used / total`. Memory metrics support immediate reads.

## Procfs metric configuration

The utilization template uses `cpu.{cpu_index}.utilization`. The `{cpu_index}`
placeholder renders native `cpu` as `total` and `cpuN` as `N`, while the original
native label remains the source of the `/proc/stat` read. `{label}` remains
available for templates that need the native label.
