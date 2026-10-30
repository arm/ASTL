<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Linux NUMA telemetry

The `numa` backend reads `/sys/devices/system/node/node*/numastat` directly; no
`numastat` command is required. It is discovered by default on Linux when readable
NUMA statistics exist. Android and non-Linux hosts do not discover this backend.
Use `ASTL_COLLECTORS=numa` to select it, `ASTL_NUMA_ROOT` to override the node root,
or `-DASTL_NUMA=OFF` to disable discovery at build time.

The `numa` target exposes metrics in the existing `memory` group and raw counters:

| Name                        | Meaning                                                        |
| --------------------------- | -------------------------------------------------------------- |
| `Linux_numa_hit`            | Pages allocated on the policy-preferred node                   |
| `Linux_numa_miss`           | Pages allocated on a different node from the policy preference |
| `Linux_numa_interleave_hit` | Pages successfully allocated according to interleave policy    |
| `Linux_numa_local_node`     | Pages allocated on the allocating CPU's node                   |
| `Linux_numa_other_node`     | Pages allocated on another CPU's node                          |
| `Linux_numa_local_percent`  | Local pages / (local + other pages) during the interval        |
| `Linux_numa_remote_percent` | Other pages / (local + other pages) during the interval        |
| `numa_nodeN_allocations`    | Local + other pages allocated on discovered node N             |

Page values are unsigned interval deltas, not rates or bytes. The first read is a
baseline and emits no values. Percentages are absolute readings: raw counters use
basis points (10000 means 100%) with COUNT units; metric values are floating-point percentages
(0–100). Both percentages are zero when there were no allocations. All raw
counters have VALUE semantics because their samples must not be differenced again.

Nodes are discovered when the session is initialized, including sparse IDs.
Each batch reads every discovered node once. Malformed or unreadable files fail
the batch and clear its baseline. Counter rollback suppresses the affected interval
and establishes a new baseline. Restart and resume also reset baselines. Reinitialize
the session to discover newly added nodes. Snapshot mode reports the interval
between start and stop. Immediate mode needs two reads; periodic mode emits after
its initial baseline. Sampling is sequential across sysfs files, not an atomic
kernel-wide snapshot.

This implements the ASTL portion of NEOPROF-1030. Integrating these counters into
sl-record's event metadata and driver remains downstream work.

Backend code is contained in `src/impl/numa/`. The shared registry, collector type,
configuration, build list, and serialized collector ID are the integration points;
common topology, metric, and collector builders need no NUMA-specific branches.
