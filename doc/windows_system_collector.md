<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Portable system utilization

ASTL builds the Windows system collector by default on Windows. Disable it with
`-DASTL_WINDOWS=OFF`. Enabling this option on another operating system is an error.
Set `ASTL_CONFIG_DIR` to the installed configuration directory and optionally set
`ASTL_COLLECTORS=procfs,windows` to select the available system backend.
Both backends expose a target named `sysutil`; neither exposes its old target
name as an alias. Backend selection still uses `procfs` and `windows`.
The ordinary C, Python, and Go discovery and collection APIs expose these metrics;
no additional public API calls are required.

| Metric                  | Unit    | Meaning                                 |
| ----------------------- | ------- | --------------------------------------- |
| `cpu.total.utilization` | percent | Aggregate logical processor busy time   |
| `cpu.N.utilization`     | percent | Busy time for logical processor index N |
| `memory.total`          | bytes   | Physical memory reported by Windows     |
| `memory.used`           | bytes   | Total minus available physical memory   |
| `memory.utilization`    | percent | 100 × used / total physical memory      |

Metrics belong to the existing `cpu` and `memory` groups. Byte values are uint64;
percentages are float64. Windows metrics use a `windows::` ID prefix. Linux uses the same metric names. The old `stat.cpu*.utilization` and
`meminfo.MemTotal`, `meminfo.MemUsed`, and `meminfo.utilization` names are
removed without aliases. Linux-only raw counters retain their existing names.

## Sampling and processor identities

The CPU backend uses PDH `Processor Information` / `% Processor Time` counters,
including `_Total`, in one persistent query. English counter paths are translated
before wildcard expansion, so discovery does not depend on the Windows display
language. Logical processor instances are sorted by numeric native group and
processor indices, excluding `_Total` and group totals. The sorted order defines
N, starting at zero. Each metric description retains its native instance identity.
This handles multiple processor groups without a 64-processor limit. CPU N is a
logical processor, not necessarily a physical core. Rediscover after a topology
change; indices are stable only while the discovered topology is unchanged.

CPU utilization needs two samples. The first immediate read primes the counters
and emits no CPU percentage. Subsequent reads report the intervening interval.
Periodic collection follows the same rule. Snapshot collection primes at start
and samples at stop. Starting or resuming a collection resets the baseline so
paused time is not included in the next reported CPU interval. An explicit read
while paused is permitted. Invalid or missing per-CPU samples are omitted rather
than reported as zero; API failures return an ASTL error.

All memory values in a batch come from one `GlobalMemoryStatusEx` call and can be
read immediately. Zero total or available bytes exceeding total is treated as
invalid accounting. PDH is queried only when CPU operations are selected.

## Comparing with Linux and Windows tools

CPU utilization measures busy time, not frequency-adjusted processor utility.
Compare it with Windows Performance Monitor's `Processor Information` /
`% Processor Time`, using the same instances and sampling interval. Task Manager
may display a different measure. Linux ASTL counts idle plus iowait as idle;
Windows accounting is not numerically identical.

Memory utilization measures physical memory, not commit charge or page-file
utilization. Windows available memory includes standby, free, and zero lists.
It is a useful counterpart to Linux MemAvailable, but the definitions differ.

The ATX and AMX companion configurations map `avg-cpu-utilization`, `cpuN-utilization`,
`cpu-utilization`, and `avg-mem-utilization` to these metrics. That configuration
must accompany the library update to use the same aliases on Windows.

## Validation

Portable API-mock tests cover batching, memory arithmetic, CPU warm-up, invalid
samples, lifecycle resets, and identities across more than 64 logical processors.
Windows-only tests exercise PDH discovery and two-sample collection and the live
memory API. Windows CI builds the backend with MSVC. Larger-machine and localized
Windows deployment testing remains useful because hosted runners have a limited
processor topology.

References: [PDH sampling](https://learn.microsoft.com/en-us/windows/win32/perfctrs/collecting-performance-data),
[language-neutral counters](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw),
and [physical memory](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex).

## Portable metric names

The Windows collector uses the same target and metric names as procfs. See
[System utilization](system_utilization.md) for the procfs naming migration and
metric-template details. Backend-specific raw metrics, metric IDs, and accounting
details are not promised to match across platforms.
