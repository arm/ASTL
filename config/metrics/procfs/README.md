<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Generic Linux and Android procfs metrics

`metrics.json` applies to Linux and Android independently of SoC, CPU architecture,
SCMI UUID, or `platform_lookup.json`. ASTL reads declarations directly from
`$ASTL_CONFIG_DIR/metrics/procfs/*.json`. Deploy this directory and
`config/groups/metric_groups.json`; no device-specific SCMI configuration is needed
when `ASTL_COLLECTORS=procfs`.

The definitions expose total, available, and used memory in bytes, memory
utilization, and aggregate/per-core CPU utilization. Memory fields come from
`/proc/meminfo`. CPU labels are discovered from `/proc/stat`, and utilization uses
the difference between successive CPU snapshots, excluding idle and iowait from
busy time. The first CPU read establishes a baseline.

The process must be allowed to read these files. Android CI uses an unprivileged
`adb shell` executable with SELinux enforcing; ordinary APK processes have
different permissions. See [Android development](../../../doc/android.md).
