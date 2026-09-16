<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL fuzz targets

Public-API fuzz targets should keep ASTL's C API, orchestrator, topology, metric
discovery, metric management, and collectors real. Only the operating-system and
hardware boundaries are replaced:

- Link `astl_fuzz_fixtures` and keep one `astl::fuzz::MinimalProcfsFixture`
  alive for the fuzzing process. Call `Reset()` before each iteration, then set
  `ASTL_PROCFS_ROOT` to `RootPath()` before ASTL is initialized.
- Use the existing `tools/mock_scmi` submodule for SCMI. Fuzz builds compile its
  `MockScmi` target and expose `ASTL_FUZZ_MOCK_SCMI_BINARY` and
  `ASTL_FUZZ_MOCK_SCMI_CONFIG` to fixture consumers. Launch it with the existing
  `tools/mock_scmi/scripts/launch_mockscmi.sh` helper, then select its
  `arm_telemetry` directory through `ASTL_SCMI_SYSFS_TELEMETRY_ROOT`. Use the
  matching cleanup script when the process exits.

The procfs fixture deliberately contains only `stat`, `meminfo`, `loadavg`, and
`uptime`. It uses regular files, needs no mount or privileges, and makes the tree
read-only after construction and after every reset.
